#ifndef EFFECT_HPP_INCLUDED
#define EFFECT_HPP_INCLUDED
#include <vector>
#include <fstream>
#include <string>
#include <iostream>
#include <unordered_map>

#include "vulkan_include.hpp"

namespace vkBasalt
{
    enum class ParamType { Float, Int, Bool, Combo, FilePath };

    struct EffectParamDesc {
        std::string key;
        std::string label;
        ParamType   type;
        double      defaultVal;
        double      minVal;
        double      maxVal;
        double      step;
        std::vector<std::string> comboOptions;
        std::string category; // optional, empty auto-detects.
        std::string tooltip = "";  // optional, empty auto-generates, shown on hover.
        int32_t specId = -1;
        size_t  specOffset = 0;
        size_t  specSize = 0;
    };

    class Effect
    {
    public:
        virtual void applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) = 0;
        virtual void updateEffect() {}
        virtual void useDepthImage(VkImageView depthImageView){};
        
        virtual void setChainPosition(bool isFirst, bool isLast) {
            isFirstInChain = isFirst;
            isLastInChain = isLast;
        }

        virtual ~Effect(){};

        // Functions for UI related Read/Updating of params.
        virtual std::string getName() const { return "unknown"; }

        virtual const std::vector<EffectParamDesc>& getParamDescs() const {
            static const std::vector<EffectParamDesc> empty;
            return empty;
        }

        // Gets current live value of a parameter by key.
        virtual double getParam(const std::string& key) const {
            auto it = m_paramValues.find(key);
            return (it != m_paramValues.end()) ? it->second : 0.0;
        }

        // Returns the maximum quality level at which a parameter is active. If current quality level > returned value, the param is disabled in UI. Default 4 = always active (iGPU minimum). Effects override as needed.
        virtual int minQualityForParam(const std::string& key) const { return 4; }

        virtual bool setParam(const std::string& key, double value) {
            auto it = m_paramValues.find(key);
            if (it == m_paramValues.end()) return false;
            if (it->second == value) return false;
            it->second = value;
            return true;
        }

    protected:
        bool isFirstInChain = false;
        bool isLastInChain = false;
        
        std::unordered_map<std::string, double> m_paramValues;

    private:
    };
} // namespace vkBasalt

#endif // EFFECT_HPP_INCLUDED
