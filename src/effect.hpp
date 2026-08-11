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
    enum class ParamType { Float, Int, Bool, Combo };

    struct EffectParamDesc {
        std::string key;
        std::string label;
        ParamType   type;
        double      defaultVal;
        double      minVal;
        double      maxVal;
        double      step;
        std::vector<std::string> comboOptions;
    };

    class Effect
    {
    public:
        void virtual applyEffect(uint32_t imageIndex, VkCommandBuffer commandBuffer) = 0;
        void virtual updateEffect(){};
        void virtual useDepthImage(VkImageView depthImageView){};
        virtual ~Effect(){};

        // Functions for UI related Read/Updating of params.
        virtual std::string getName() const { return "unknown"; }

        virtual const std::vector<EffectParamDesc>& getParamDescs() const {
            static const std::vector<EffectParamDesc> empty;
            return empty;
        }

        // Gets current live value of a parameter by key.
        virtual double getParam(const std::string& key) const { return 0.0; }

        virtual bool setParam(const std::string& key, double value) { return false; }

    private:
    };
} // namespace vkBasalt

#endif // EFFECT_HPP_INCLUDED
