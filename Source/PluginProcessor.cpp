/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"



//==============================================================================
SimpleMBCompAudioProcessor::SimpleMBCompAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
}

SimpleMBCompAudioProcessor::~SimpleMBCompAudioProcessor()
{
}

//==============================================================================
const juce::String SimpleMBCompAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool SimpleMBCompAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool SimpleMBCompAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool SimpleMBCompAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double SimpleMBCompAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int SimpleMBCompAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int SimpleMBCompAudioProcessor::getCurrentProgram()
{
    return 0;
}

void SimpleMBCompAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String SimpleMBCompAudioProcessor::getProgramName (int index)
{
    return {};
}

void SimpleMBCompAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void SimpleMBCompAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Use this method as the place to do any pre-playback
    // initialisation that you need..
    
    juce::dsp::ProcessSpec spec;
    spec.numChannels = 1;
    spec.maximumBlockSize = samplesPerBlock;
    spec.sampleRate = sampleRate;
    
    leftChain.prepare(spec);
    rightChain.prepare(spec);
    
    ChainSettings chainSettings = getChainSettings(apvts);
    
    auto peakCoefs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                        sampleRate,
                        chainSettings.peakFreq,
                        chainSettings.peakQuality,
                        juce::Decibels::decibelsToGain(chainSettings.peakGainDb));
    
    *leftChain.get<ChainPosition::Peak>().coefficients = *peakCoefs;
    *rightChain.get<ChainPosition::Peak>().coefficients = *peakCoefs;

    auto cutCoefs =
        juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(chainSettings.lowCutFreq,
                                                                               sampleRate,
                                                                               2 * (chainSettings.lowCutSlope + 1));
    auto& leftLowCut = leftChain.get<ChainPosition::LowPass>();
    auto& rightLowCut = rightChain.get<ChainPosition::LowPass>();
    
    leftLowCut.setBypassed<0>(true);
    leftLowCut.setBypassed<1>(true);
    leftLowCut.setBypassed<2>(true);
    leftLowCut.setBypassed<3>(true);
    rightLowCut.setBypassed<0>(true);
    rightLowCut.setBypassed<1>(true);
    rightLowCut.setBypassed<2>(true);
    rightLowCut.setBypassed<3>(true);

    switch(chainSettings.lowCutSlope)
    {
        case Slope_48:
        {
            leftLowCut.get<3>().coefficients = *cutCoefs[3];
            leftLowCut.setBypassed<3>(false);
            rightLowCut.get<3>().coefficients = *cutCoefs[3];
            rightLowCut.setBypassed<3>(false);
        }
        case Slope_36:
        {
            leftLowCut.get<2>().coefficients = *cutCoefs[2];
            leftLowCut.setBypassed<2>(false);
            rightLowCut.get<2>().coefficients = *cutCoefs[2];
            rightLowCut.setBypassed<2>(false);
        }
        case Slope_24:
        {
            leftLowCut.get<1>().coefficients = *cutCoefs[1];
            leftLowCut.setBypassed<1>(false);
            rightLowCut.get<1>().coefficients = *cutCoefs[1];
            rightLowCut.setBypassed<1>(false);
        }
        case Slope_12:
        {
            leftLowCut.get<0>().coefficients = *cutCoefs[0];
            leftLowCut.setBypassed<0>(false);
            rightLowCut.get<0>().coefficients = *cutCoefs[0];
            rightLowCut.setBypassed<0>(false);
            break;
        }
    }
}

void SimpleMBCompAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool SimpleMBCompAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void SimpleMBCompAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
    {
        buffer.clear (i, 0, buffer.getNumSamples());
    }
    
    ChainSettings chainSettings = getChainSettings(apvts);
    
    auto peakCoefs = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
                        getSampleRate(),
                        chainSettings.peakFreq,
                        chainSettings.peakQuality,
                        juce::Decibels::decibelsToGain(chainSettings.peakGainDb));
    
    *leftChain.get<ChainPosition::Peak>().coefficients = *peakCoefs;
    *rightChain.get<ChainPosition::Peak>().coefficients = *peakCoefs;
    
    juce::dsp::AudioBlock<float> block(buffer);
    auto leftBlock = block.getSingleChannelBlock(0);
    auto rightBlock = block.getSingleChannelBlock(1);
    
    juce::dsp::ProcessContextReplacing<float> leftContext(leftBlock);
    juce::dsp::ProcessContextReplacing<float> rightContext(rightBlock);
    
    leftChain.process(leftContext);
    rightChain.process(rightContext);
    
    auto cutCoefs =
        juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(chainSettings.lowCutFreq,
                                                                               getSampleRate(),
                                                                               2 * (chainSettings.lowCutSlope + 1));
    auto& leftLowCut = leftChain.get<ChainPosition::LowPass>();
    auto& rightLowCut = rightChain.get<ChainPosition::LowPass>();
    
    leftLowCut.setBypassed<0>(true);
    leftLowCut.setBypassed<1>(true);
    leftLowCut.setBypassed<2>(true);
    leftLowCut.setBypassed<3>(true);
    rightLowCut.setBypassed<0>(true);
    rightLowCut.setBypassed<1>(true);
    rightLowCut.setBypassed<2>(true);
    rightLowCut.setBypassed<3>(true);

    switch(chainSettings.lowCutSlope)
    {
        case Slope_48:
        {
            leftLowCut.get<3>().coefficients = *cutCoefs[3];
            leftLowCut.setBypassed<3>(false);
            rightLowCut.get<3>().coefficients = *cutCoefs[3];
            rightLowCut.setBypassed<3>(false);
        }
        case Slope_36:
        {
            leftLowCut.get<2>().coefficients = *cutCoefs[2];
            leftLowCut.setBypassed<2>(false);
            rightLowCut.get<2>().coefficients = *cutCoefs[2];
            rightLowCut.setBypassed<2>(false);
        }
        case Slope_24:
        {
            leftLowCut.get<1>().coefficients = *cutCoefs[1];
            leftLowCut.setBypassed<1>(false);
            rightLowCut.get<1>().coefficients = *cutCoefs[1];
            rightLowCut.setBypassed<1>(false);
        }
        case Slope_12:
        {
            leftLowCut.get<0>().coefficients = *cutCoefs[0];
            leftLowCut.setBypassed<0>(false);
            rightLowCut.get<0>().coefficients = *cutCoefs[0];
            rightLowCut.setBypassed<0>(false);
            break;
        }
    }
}

//==============================================================================
bool SimpleMBCompAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* SimpleMBCompAudioProcessor::createEditor()
{
    //return new SimpleMBCompAudioProcessorEditor (*this);
    return new juce::GenericAudioProcessorEditor(*this);
}

//==============================================================================
void SimpleMBCompAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void SimpleMBCompAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

ChainSettings getChainSettings(juce::AudioProcessorValueTreeState& apvts)
{
    ChainSettings settings;
    
    settings.lowCutFreq = apvts.getRawParameterValue("LowCut Freq")->load();
    settings.highCutFreq = apvts.getRawParameterValue("HighCut Freq")->load();
    settings.peakFreq = apvts.getRawParameterValue("Peak Freq")->load();
    settings.peakGainDb = apvts.getRawParameterValue("Peak Gain")->load();
    settings.peakQuality = apvts.getRawParameterValue("Peak Quality")->load();
    settings.lowCutSlope = static_cast<Slope>(apvts.getRawParameterValue("LowCut Slope")->load());
    settings.highCutSlope = static_cast<Slope>(apvts.getRawParameterValue("HighCut Slope")->load());
    
    return settings;
}

juce::AudioProcessorValueTreeState::ParameterLayout
    SimpleMBCompAudioProcessor::createParameterLayout()
{
    // create layout-object
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
        
    layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("LowCut Freq",1),
            "LowCut Frequency",
            juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f),
            20.0f
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("HighCut Freq",1),
            "HighCut Frequency",
            juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f),
            20000.0f
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("Peak Freq",1),
            "Peak Frequency",
            juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.25f),
            750.0f
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("Peak Gain",1),
            "Peak Gain",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.5f, 1.0f),
            0.0f
    ));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("Peak Quality",1),
            "Peak Qaulity",
            juce::NormalisableRange<float>(0.1f, 10.0f, 0.05f, 1.0f),
            1.0f
    ));
        
    juce::StringArray stringArray;
    for(int i = 0; i < 4; ++i)
    {
        juce::String str;
        str << (12 +i * 12);
        str << " db/Oct";
        stringArray.add(str);
    }
        
    layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID("LowCut Slope",1),
            "LowCut Slope",
            stringArray,
            0
    ));
        
    layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID("HighCut Slope",1),
            "HighCut Slope",
            stringArray,
            0
    ));
        
    return layout;
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleMBCompAudioProcessor();
}
