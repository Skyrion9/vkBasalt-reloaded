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
layout(constant_id = 11) const float targetWhite = 2.03;
layout(constant_id = 12) const float targetPeak = 10.0;
layout(constant_id = 13) const float peakScale = 1.0;
layout(constant_id = 14) const float midtoneRange = 0.05;

layout(push_constant) uniform PushConstants {
    float deltaTime;
} pc;

shared uint sharedTotal;
shared uint sharedPrefix[256];

void main() {
    uint tid = gl_LocalInvocationID.x;
    
    if (tid == 0) sharedTotal = 0;
    barrier();
    
    atomicAdd(sharedTotal, bins[tid]);
    barrier();
    
    uint totalPixels = sharedTotal;
    if (totalPixels == 0) {
        if (tid == 0) {
            metrics.adaptiveWhite = targetWhite;
            metrics.adaptivePeak = targetPeak;
            metrics.intensity = 1.0;
        }
        return;
    }
    
    uint val = bins[tid];
    sharedPrefix[tid] = val;
    barrier();
    
    // Inclusive prefix sum (Blelloch scan)
    for (uint offset = 1; offset < 256; offset *= 2) {
        uint temp = 0;
        if (tid >= offset) temp = sharedPrefix[tid - offset];
        barrier();
        sharedPrefix[tid] += temp;
        barrier();
    }
    
    if (tid == 0) {
        uint targetP99 = uint(float(totalPixels) * 0.99);
        float p99Luma = 1.0;
        float weightedSum = 0.0;
        
        for (int i = 0; i < 256; i++) {
            weightedSum += float(bins[i]) * (float(i) / 255.0);
            if (sharedPrefix[i] >= targetP99 && p99Luma == 1.0) {
                p99Luma = float(i) / 255.0;
            }
        }
        float avgLuma = weightedSum / float(totalPixels);
        
        float alpha = 1.0 - exp(-pc.deltaTime * adaptationSpeed);
        float newP99 = temporal.smoothedP99 + alpha * (p99Luma - temporal.smoothedP99);
        float newAvg = temporal.smoothedAvg + alpha * (avgLuma - temporal.smoothedAvg);
        
        temporal.smoothedP99 = newP99;
        temporal.smoothedAvg = newAvg;
        
        float midtoneBias = mix(1.0 + midtoneRange, 1.0 - midtoneRange, smoothstep(0.2, 0.8, newAvg));
        float sceneIntensity = mix(1.0, 0.2, smoothstep(0.5, 1.0, newP99));
        float finalIntensity = mix(sceneIntensity, 1.0, peakScale);
        
        metrics.adaptiveWhite = targetWhite * midtoneBias;
        metrics.adaptivePeak = mix(targetWhite, targetPeak, finalIntensity);
        metrics.intensity = finalIntensity;
    }
}
