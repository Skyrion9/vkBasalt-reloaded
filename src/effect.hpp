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
        virtual double getParam(const std::string& key) const { return 0.0; }

        virtual bool setParam(const std::string& key, double value) { return false; }

    protected:
        bool isFirstInChain = false;
        bool isLastInChain = false;

    private:
    };
} // namespace vkBasalt

#endif // EFFECT_HPP_INCLUDED
