#version 450

layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;

layout(set = 0, binding = 0) buffer HistogramBuffer { uint bins[256]; };
layout(set = 0, binding = 1) buffer TemporalBuffer { 
    float smoothedP99;
    float smoothedAvg;
    float padding1;
    float padding2;
} temporal;
layout(set = 0, binding = 2) buffer MetricsBuffer { 
    float adaptiveWhite;
    float adaptivePeak;
    float intensity;
    float padding;
} metrics;

layout(constant_id = 10) const float adaptationSpeed = 0.1;
layout(constant_id = 11) const float targetWhite     = 2.03;
layout(constant_id = 12) const float targetPeak      = 10.0;
layout(constant_id = 13) const float peakScale       = 1.0;
layout(constant_id = 14) const float midtoneRange    = 0.05;
layout(constant_id = 15) const int   calibrationMode = 0;    // 0=adaptive expansion, 1=passthrough (measure only)

layout(push_constant) uniform PushConstants {
    float deltaTime;
} pc;

shared uint sharedPrefix[256];
shared float sharedWeighted[256];

void main() {
    uint tid = gl_LocalInvocationID.x;
    
    uint val = bins[tid];
    sharedPrefix[tid] = val;
    barrier();
    
    // Inclusive prefix sum (Hillis Steele scan)
    for (uint offset = 1; offset < 256; offset *= 2) {
        uint temp = 0;
        if (tid >= offset) temp = sharedPrefix[tid - offset];
        barrier();
        sharedPrefix[tid] += temp;
        barrier();
    }

    uint totalPixels = sharedPrefix[255];
    
    if (totalPixels == 0) {
        if (tid == 0) {
            metrics.adaptiveWhite = targetWhite;
            metrics.adaptivePeak = targetPeak;
            metrics.intensity = 1.0;
        }
        return;
    }
    
    // Parallel weighted sum reduction (avoids serial 256-iteration loop on thread 0)
    float binLuma = (float(tid) + 0.5) / 256.0;
    float linearLuma = binLuma / (1.0 - binLuma);
    
    sharedWeighted[tid] = float(bins[tid]) * linearLuma;
    barrier();
    
    for (uint offset = 128; offset > 0; offset >>= 1) {
        if (tid < offset) {
            sharedWeighted[tid] += sharedWeighted[tid + offset];
        }
        barrier();
    }
    
    if (tid == 0) {
        uint targetP99 = uint(float(totalPixels) * 0.99);
        float p99Luma = 0.0;
        
        // Thread 0 finds P99 (256 iterations is trivial)
        for (int i = 0; i < 256; i++) {
            if (sharedPrefix[i] >= targetP99) {
                float bL = (float(i) + 0.5) / 256.0;
                p99Luma = bL / (1.0 - bL);
                break;
            }
        }
        float avgLuma = sharedWeighted[0] / float(totalPixels);

        // Guard against uninitialized DEVICE_LOCAL memory (NaN or out of range garbage). This handles the edge case where the first acquired swapchain image is not index 0.
        bool temporalValid = (temporal.smoothedP99 == temporal.smoothedP99) &&
                             (temporal.smoothedAvg == temporal.smoothedAvg) &&
                             (temporal.smoothedP99 >= 0.0 && temporal.smoothedP99 <= 100.0) &&
                             (temporal.smoothedAvg >= 0.0 && temporal.smoothedAvg <= 100.0);

        if (!temporalValid) {
            temporal.smoothedP99 = p99Luma;
            temporal.smoothedAvg = avgLuma;
        } else {
            float alpha = 1.0 - exp(-pc.deltaTime * adaptationSpeed);
            temporal.smoothedP99 = temporal.smoothedP99 + alpha * (p99Luma - temporal.smoothedP99);
            temporal.smoothedAvg = temporal.smoothedAvg + alpha * (avgLuma - temporal.smoothedAvg);
        }
        
        if (calibrationMode == 1) {
            // Passthrough: Report raw measurements as metadata. The image passes through unchanged, but the display gets accurate MaxCLL/MaxFALL.
            metrics.adaptiveWhite = temporal.smoothedAvg;                  // Measured frame average (MaxFALL)
            metrics.adaptivePeak  = min(temporal.smoothedP99, targetPeak); // Measured peak (MaxCLL), capped at display limit
            metrics.intensity     = 1.0;
        } else {
            // Adaptive: Modulate gain based on scene brightness
            float avgRatio = clamp(temporal.smoothedAvg / targetWhite, 0.0, 2.0);
            float midtoneBias = mix(1.0 + midtoneRange, 1.0 - midtoneRange, smoothstep(0.5, 1.5, avgRatio));
            
            // Normalize P99 relative to targetPeak to determine scene intensity
            float peakRatio = clamp(temporal.smoothedP99 / targetPeak, 0.0, 1.0);
            float sceneIntensity = mix(1.0, 0.2, smoothstep(0.5, 1.0, peakRatio));
            float finalIntensity = mix(sceneIntensity, 1.0, peakScale);
            
            metrics.adaptiveWhite = targetWhite * midtoneBias;
            metrics.adaptivePeak = mix(targetWhite, targetPeak, finalIntensity);
            metrics.intensity = finalIntensity;
        }
    }
}
