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

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace foleys
{

class MagicLevelSource;

class MagicLevelMeter : public juce::Component,
                        public juce::SettableTooltipClient,
                        private juce::Timer
{
public:
    enum ColourIds
    {
        backgroundColourId = 0x2002100,
        barBackgroundColourId,
        barFillColourId,
        outlineColourId,
        tickmarkColourId
    };

    //==========================================================================
    // JOS FORK ADDITION (2026-08-13): WHAT THE PUSHED VALUES MEAN, and the
    // meter's own state.
    //
    // A MagicLevelSource is a bag of numbers -- pushSamples() stores a peak and
    // an RMS and that is all it knows.  The meter is where those numbers become
    // a READING, and a reading needs three things the widget could not
    // previously be told: how far down its floor is, whether "up" means "more
    // signal" or "more gain reduction", and (for a level) whether the signal has
    // been over full scale since anyone last looked.
    //
    // WHY IT LIVES HERE AND NOT IN THE LookAndFeel: a LookAndFeel is ONE object
    // shared by every widget in the GUI (PGM registers one per builder), so it
    // has nowhere to keep per-meter anything.  The scale is per meter and the
    // clip latch is per meter per channel.
    //
    // WHY IT IS NOT DERIVED FROM THE DATA: it cannot be.  The `limiterGR` meter
    // in this fleet is fed 10^(-5*(1 - GR/12)), a value STRETCHED so that 12 dB
    // of gain reduction fills a bar whose floor is -100 dB (jos_GuitarParent.cpp,
    // the GR push site).  Read as dBFS on a 60 dB scale it shows nothing until
    // the limiter is already taking 4.8 dB off.  Nothing in the buffer says so;
    // the layout has to.

    /** What the pushed values mean, which is what the scale and its labels
        mean.  Set from a layout with `meter-scale="dbfs" | "gain-reduction"`. */
    enum class Scale
    {
        dBFS = 0,       ///< a LEVEL: full scale at the top, getRangeDb() dB of span below it
        gainReduction   ///< a REDUCTION: empty when the limiter is idle, full at
                        ///< gainReductionFullScaleDb of gain reduction
    };

    /** Which way the bars run.  `automatic` (the default) is decided by the
        shape the layout gives us -- see JosLookAndFeel::meterIsHorizontal. */
    enum class Orientation { automatic = 0, vertical, horizontal };

    /** dB below full scale at the bottom of a `dBFS` meter.  60 is the standard
        mixing-desk span; the widget's old hard-wired -100 crushed everything
        anyone actually listens to into the top third of the bar. */
    static constexpr float defaultRangeDb = 60.0f;

    /** dB span of a `gainReduction` meter.  NOT a taste choice: it is dictated
        by the stretched value the GR push site sends (see above), and the two
        numbers have to agree or the bar reads wrong. */
    static constexpr float gainReductionRangeDb = 100.0f;

    /** Gain reduction, in dB, at the top of a `gainReduction` meter.  Must equal
        `kGRMeterFullScaleDb` in jos_GuitarParent.cpp's GR push site; that is the
        constant this one is the display half of. */
    static constexpr float gainReductionFullScaleDb = 12.0f;

    struct LookAndFeelMethods
    {
        virtual ~LookAndFeelMethods()=default;
        virtual void drawLevelMeter (juce::Graphics& g,
                                     MagicLevelMeter& meter,
                                     MagicLevelSource* source,
                                     juce::Rectangle<int> bounds) = 0;
    };

    MagicLevelMeter();

    void paint (juce::Graphics& g) override;

    void setLevelSource (MagicLevelSource* newSource);
    MagicLevelSource* getLevelSource() const noexcept       { return source; }

    void setScale (Scale newScale);
    Scale getScale() const noexcept                         { return scale; }

    /** Overrides the scale's default span.  Zero or less restores the default. */
    void setRangeDb (float newRangeDb);

    /** The span actually drawn: the explicit one if a layout set it, else the
        scale's default. */
    float getRangeDb() const noexcept;

    void setOrientation (Orientation newOrientation);
    Orientation getOrientation() const noexcept             { return orientation; }

    /** Whether a dB scale may be drawn beside the bars.  True (the default)
        means "if it fits"; the LookAndFeel decides, because only it knows how
        much room its own tick text needs. */
    void setTickmarksEnabled (bool shouldBeEnabled);
    bool areTickmarksEnabled() const noexcept               { return tickmarks; }

    //==========================================================================
    // THE CLIP LATCH.  A peak over full scale is the one thing a level meter
    // must not let you miss, and it is exactly the thing a 30 Hz repaint of an
    // instantaneous value does miss.  So it is LATCHED: once a channel's peak
    // reaches full scale it stays flagged until someone clears it, and clicking
    // the meter is what clears it.
    //
    // Latched only for Scale::dBFS.  "Over full scale" has no meaning on a gain
    // reduction meter, whose full scale is 12 dB of limiting.

    /** True when `channel` has hit full scale since the last clear. */
    bool isClipped (int channel) const noexcept;

    /** True when any channel has. */
    bool isAnyChannelClipped() const noexcept;

    /** Clears every channel's latch (what a click does). */
    void clearClipped();

    void mouseDown (const juce::MouseEvent& event) override;

    void timerCallback() override;

private:
    juce::WeakReference<MagicLevelSource> source;

    Scale       scale       = Scale::dBFS;
    float       explicitRangeDb = 0.0f;          // 0 => the scale's default
    Orientation orientation = Orientation::automatic;
    bool        tickmarks   = true;

    /** One latch per channel, sized from the source as it appears.  `char` and
        not `bool` because std::vector<bool> is a bit field, and this is read
        from paint() while timerCallback() writes it -- both on the message
        thread, but a proxy-reference container is a trap not worth leaving. */
    std::vector<char> clipped;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MagicLevelMeter)
};


} // namespace foleys
