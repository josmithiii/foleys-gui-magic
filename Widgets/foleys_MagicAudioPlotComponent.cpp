/*
 ==============================================================================
    Copyright (c) 2019-2021 Foleys Finest Audio - Daniel Walz
    All rights reserved.

    License for non-commercial projects:

    Redistribution and use in source and binary forms, with or without modification,
    are permitted provided that the following conditions are met:
    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    License for commercial products:

    To sell commercial products containing this module, you are required to buy a
    License from https://foleysfinest.com/developer/pluginguimagic/

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
    ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
    BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
    OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
    OF THE POSSIBILITY OF SUCH DAMAGE.
 ==============================================================================
 */


namespace foleys
{


MagicAudioPlotComponent::MagicAudioPlotComponent()
{
    setColour (plotColourId, juce::Colours::orange);
    setColour (plotFillColourId, juce::Colours::orange.withAlpha (0.5f));
    setColour (plotInactiveColourId, juce::Colours::orange.darker());
    setColour (plotInactiveFillColourId, juce::Colours::orange.darker().withAlpha (0.5f));

    setOpaque (false);
    setPaintingIsUnclipped (true);
}

void MagicAudioPlotComponent::setPlotSource (MagicAudioPlotSource* source)
{
    plotSource = dynamic_cast<MagicAudioPlotSource*>(source);
    jassert(plotSource != nullptr);
}

void MagicAudioPlotComponent::setDecayFactor (float decayFactor)
{
    decay = decayFactor;
    updateGlowBufferSize();
}

void MagicAudioPlotComponent::setTriggered (bool t)
{
    triggered = t;
    if (plotSource)
      plotSource->setTriggered (triggered);
}

void MagicAudioPlotComponent::setOverlay (bool o)
{
    overlay = o;
    if (plotSource)
      plotSource->setOverlay (overlay);
}

void MagicAudioPlotComponent::setChannel (int c)
{
    channel = c;
    if (plotSource)
      plotSource->setChannel (channel);
}

void MagicAudioPlotComponent::setNumChannels (int nc)
{
    numChannels = nc;
    if (plotSource)
      plotSource->setNumChannels (numChannels);
}

void MagicAudioPlotComponent::setPlotLength (int pl)
{
    plotLength = pl;
    if (plotSource)
      plotSource->setPlotLength (plotLength);
}

void MagicAudioPlotComponent::setPlotOffset (int pl)
{
    plotOffset = pl;
    if (plotSource)
      plotSource->setPlotOffset (plotOffset);
}

void MagicAudioPlotComponent::paint (juce::Graphics& g)
{
    if (plotSource == nullptr)
        return;

    const auto lastUpdate = plotSource->getLastDataUpdate();
    if (lastUpdate > lastDataTimestamp)
    {
        if (plotSource) { // these may be have been set before plotSource existed:
            plotSource->setTriggered (triggered);
            plotSource->setOverlay (overlay);
            plotSource->setChannel (channel);
            plotSource->setNumChannels (numChannels);
            plotSource->setPlotLength (plotLength);
            plotSource->setPlotOffset (plotOffset);
        }
        plotSource->createPlotPaths (path, filledPath, getLocalBounds().toFloat(), *this);
        lastDataTimestamp = lastUpdate;
    }

    if (! glowBuffer.isNull())
        drawPlotGlowing (g);
    else
    {
        drawPlot (g);
    }
}

} // namespace foleys
