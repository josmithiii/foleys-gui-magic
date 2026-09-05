/*
 ==============================================================================
    Copyright (c) 2019-2023 Foleys Finest Audio - Daniel Walz
    All rights reserved.

    **BSD 3-Clause License**

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

 ==============================================================================

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

#include "foleys_MagicLevelMeter.h"
#include "../Visualisers/foleys_MagicLevelSource.h"

#include <algorithm>

namespace foleys
{

MagicLevelMeter::MagicLevelMeter()
{
    setColour (backgroundColourId, juce::Colours::transparentBlack);
    setColour (barBackgroundColourId, juce::Colours::darkgrey);
    setColour (barFillColourId, juce::Colours::darkgreen);
    setColour (outlineColourId, juce::Colours::silver);
    setColour (tickmarkColourId, juce::Colours::silver);

    startTimerHz (30);
}

//==============================================================================
// JOS FORK ADDITION (2026-08-13).  See the header for what each of these is
// for; all of them are set from a layout by LevelMeterItem.

void MagicLevelMeter::setScale (Scale newScale)
{
    if (scale == newScale)
        return;

    scale = newScale;

    // A gain reduction meter cannot be "over", so a latch inherited from a
    // previous configuration would be stuck on with nothing able to set it.
    if (scale != Scale::dBFS)
        clearClipped();

    repaint();
}

void MagicLevelMeter::setRangeDb (float newRangeDb)
{
    explicitRangeDb = newRangeDb > 0.0f ? newRangeDb : 0.0f;
    repaint();
}

float MagicLevelMeter::getRangeDb() const noexcept
{
    if (explicitRangeDb > 0.0f)
        return explicitRangeDb;

    return scale == Scale::gainReduction ? gainReductionRangeDb : defaultRangeDb;
}

void MagicLevelMeter::setOrientation (Orientation newOrientation)
{
    if (orientation == newOrientation)
        return;

    orientation = newOrientation;
    repaint();
}

void MagicLevelMeter::setTickmarksEnabled (bool shouldBeEnabled)
{
    if (tickmarks == shouldBeEnabled)
        return;

    tickmarks = shouldBeEnabled;
    repaint();
}

bool MagicLevelMeter::isClipped (int channel) const noexcept
{
    return juce::isPositiveAndBelow (channel, int (clipped.size()))
             && clipped [size_t (channel)] != 0;
}

bool MagicLevelMeter::isAnyChannelClipped() const noexcept
{
    for (auto c : clipped)
        if (c != 0)
            return true;

    return false;
}

void MagicLevelMeter::clearClipped()
{
    std::fill (clipped.begin(), clipped.end(), char (0));
    repaint();
}

void MagicLevelMeter::mouseDown (const juce::MouseEvent&)
{
    clearClipped();
}

void MagicLevelMeter::paint (juce::Graphics& g)
{
    if (auto* lnf = dynamic_cast<LookAndFeelMethods*>(&getLookAndFeel()))
    {
        lnf->drawLevelMeter (g, *this, source, getLocalBounds());
        return;
    }

    const auto backgroundColour = findColour (backgroundColourId);
    if (!backgroundColour.isTransparent())
        g.fillAll (backgroundColour);

    if (source == nullptr)
        return;

    const auto numChannels = source->getNumChannels();
    if (numChannels == 0)
        return;

    auto bounds = getLocalBounds().reduced (3).toFloat();

    const auto width = bounds.getWidth() / numChannels;
    const auto barBackgroundColour = findColour (barBackgroundColourId);
    const auto barFillColour = findColour (barFillColourId);
    const auto outlineColour = findColour (outlineColourId);

    // JOS FORK (2026-08-13): was a hard-wired -100.  A widget that carries a
    // configured range and then draws a different one is worse than one that
    // never had the setting, so the fallback drawing honours it too -- which
    // also gives the plugins that do NOT use the JOS LookAndFeel (Glow,
    // Parallax, Vowelator) the standard 60 dB span instead of a scale that put
    // every level anyone listens to in the top third of the bar.
    const auto infinity = -getRangeDb();
    for (int i=0; i < numChannels; ++i)
    {
        auto bar = bounds.removeFromLeft (width).reduced (1);
        g.setColour (barBackgroundColour);
        g.fillRect (bar);
        g.setColour (outlineColour);
        g.drawRect (bar, 1.0f);
        bar.reduce (1, 1);
        g.setColour (barFillColour);
        g.fillRect (bar.withTop (juce::jmap (juce::Decibels::gainToDecibels (source->getRMSvalue (i), infinity),
                                             infinity, 0.0f, bar.getBottom(), bar.getY())));
        g.drawHorizontalLine (juce::roundToInt (juce::jmap (juce::Decibels::gainToDecibels (source->getMaxValue (i), infinity),
                                                            infinity, 0.0f, bar.getBottom (), bar.getY ())),
                              static_cast<float>(bar.getX ()), static_cast<float>(bar.getRight ()));
    }
}

void MagicLevelMeter::setLevelSource (MagicLevelSource* newSource)
{
    source = newSource;
}

void MagicLevelMeter::timerCallback()
{
    // JOS FORK (2026-08-13): the clip latch is sampled HERE and not in paint(),
    // because paint() is not guaranteed to run (an obscured or hidden meter is
    // not painted) and a clip that happened while the window was behind another
    // one is exactly the clip you want to find afterwards.
    //
    // 30 Hz against MagicLevelSource's max, which is held for the source's
    // maxKeepMS (500 ms in this fleet): the hold is more than an order of
    // magnitude longer than the poll, so nothing gets past this.
    if (scale == Scale::dBFS && source != nullptr)
    {
        const auto numChannels = size_t (source->getNumChannels());

        if (clipped.size() != numChannels)
            clipped.assign (numChannels, char (0));

        for (size_t c = 0; c < numChannels; ++c)
            if (source->getMaxValue (int (c)) >= 1.0f)
                clipped [c] = 1;
    }

    // BEGIN JOS (2026-09-05): a meter that is NOT ON SCREEN must not repaint.
    //
    // The clip latch above deliberately keeps running -- that is the whole
    // point of sampling it here rather than in paint(), and a meter behind
    // another window is still showing -- but the 30 Hz repaint of a meter with
    // no peer is pure waste.  It matters now because the view cache
    // (MagicGUIBuilder::setViewCacheEnabled) keeps a whole PARKED layout alive
    // while another one is on screen, and every timer in that layout keeps
    // ticking; this is the same isShowing() stand-down every self-polling item
    // in jos-juce-plugins already does.
    //
    // Self-healing: the next tick after it comes back repaints it, i.e. within
    // 33 ms, and paint() reads the source live.
    if (isShowing())
        repaint();
    // END JOS
}

} // namespace foleys
