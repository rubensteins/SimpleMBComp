/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

enum Slope
{
    Slope_12,
    Slope_24,
    Slope_36,
    Slope_48
};

struct ChainSettings
{
    float peakFreq = 0.0f;
    float peakGainDb = 0.0f;
    float peakQuality = 1.0f;
    float lowCutFreq = 0.0f;
    float highCutFreq = 0.0f;

    Slope lowCutSlope = Slope::Slope_12;
    Slope highCutSlope = Slope::Slope_12;
};

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts);

//==============================================================================
/**
*/
class SimpleMBCompAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    SimpleMBCompAudioProcessor();
    ~SimpleMBCompAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // this is a function that returns all paramters
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts {*this, nullptr, "Parameters", createParameterLayout()};
    
private:
    
    void updatePeakfilter(ChainSettings settings);
    
    // aliases
    using Filter = juce::dsp::IIR::Filter<float>;
    
    using CutFilter = juce::dsp::ProcessorChain<Filter, Filter, Filter, Filter>;
    using MonoChain = juce::dsp::ProcessorChain<CutFilter, Filter, CutFilter>;
    MonoChain leftChain, rightChain;
    
    enum ChainPosition
    {
        LowPass,
        Peak,
        HighPass
    };
    
    template<typename ChainType, typename CoefficentType>
    void updateLowCutFilter(ChainType& chain,
                            const CoefficentType& coefficients,
                            const Slope& lowCutSlope)
    {
        chain.template setBypassed<0>(true);
        chain.template setBypassed<1>(true);
        chain.template setBypassed<2>(true);
        chain.template setBypassed<3>(true);

        switch(lowCutSlope)
        {
            case Slope_48:
            {
                chain.template get<3>().coefficients = *coefficients[3];
                chain.template setBypassed<3>(false);
            }
            case Slope_36:
            {
                chain.template get<2>().coefficients = *coefficients[2];
                chain.template setBypassed<2>(false);
            }
            case Slope_24:
            {
                chain.template get<1>().coefficients = *coefficients[1];
                chain.template setBypassed<1>(false);
            }
            case Slope_12:
            {
                chain.template get<0>().coefficients = *coefficients[0];
                chain.template setBypassed<0>(false);
                break;
            }
        }
    }
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleMBCompAudioProcessor)
};
