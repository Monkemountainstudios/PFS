#include <juce_audio_utils/juce_audio_utils.h>
#include <iostream>

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::AudioDeviceManager devices;
    juce::StringArray errors;
    for (auto* type : devices.getAvailableDeviceTypes())
    {
        type->scanForDevices();
        const auto outputs = type->getDeviceNames (false);
        std::cout << type->getTypeName() << ": " << outputs.joinIntoString (", ") << std::endl;
        if (outputs.isEmpty())
            continue;

        for (const auto& output : outputs)
        {
            devices.closeAudioDevice();

            devices.setCurrentAudioDeviceType (type->getTypeName(), false);

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            setup.outputDeviceName = output;
            setup.inputDeviceName.clear();
            setup.useDefaultInputChannels = false;
            setup.useDefaultOutputChannels = false;
            setup.outputChannels.setRange (0, 2, true);

            const auto error = devices.setAudioDeviceSetup (setup, false);
            auto* device = devices.getCurrentAudioDevice();
            if (error.isEmpty() && device != nullptr && device->isOpen())
            {
                std::cout << "Opened " << type->getTypeName() << ": " << device->getName()
                          << " at " << device->getCurrentSampleRate() << " Hz / "
                          << device->getCurrentBufferSizeSamples() << " samples" << std::endl;
                devices.closeAudioDevice();
                return 0;
            }

            errors.add (type->getTypeName() + " / " + output + ": " + error);
        }
    }

    std::cerr << "All JUCE devices failed:\n" << errors.joinIntoString ("\n") << std::endl;
    return 1;
}
