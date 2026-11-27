// The Advanced engine's lab (Bass Architecture 2.0, P2 / P3): renders patches written as preset rows
// ("advEngine=1 advOscAOn=1 ...", the BassFactoryPresets.inc syntax) through the whole plugin path (engine, master,
// side) over a phrase, writes stereo WAVs and measures each render, so sounds can be designed and checked without a
// rebuild.
//
//   HPDG_Bass808Snapshot --adv-render <spec.txt> <out folder> [bpm]
//     spec: one patch per line "name | settings | phrase"; phrase "note:start:length ..." in beats (empty: a
//     two-bar DnB line in G). Lines starting with # are skipped.
//   writes <out>/<name>.wav (48 kHz, 24 bit, stereo) and <out>/metrics.tsv:
//     rms / peak dBFS, band shares (sub < 100 Hz, low-mid 100-300, mid 300-2k, high > 2k, of the mono sum), side / mid
//     energy over the whole band and under 120 Hz (a mono sub: ~0), L / R correlation, fundamental error in cents
//     of the first note's sub band (zero crossings), real-time factor of the render.
#include <cmath>
#include <cstdio>
#include "Bass808/Bass808Processor.h"
#include "Bass808/BassEngine.h"
#include "Bass808/BassSettingsIO.h"

namespace
{
struct Note
{
    int note;
    double start, length;
};

std::vector<Note> parsePhrase(const juce::String& text)
{
    std::vector<Note> notes;
    for (const auto& token : juce::StringArray::fromTokens(text, " ", ""))
    {
        const auto parts = juce::StringArray::fromTokens(token, ":", "");
        if (parts.size() == 3)
            notes.push_back({ parts[0].getIntValue(), parts[1].getDoubleValue(), parts[2].getDoubleValue() });
    }
    if (notes.empty())
        // a Dancefloor drop line (G minor, 2 bars): long root, a re-attack, a move to Bb and F, back to G
        notes = { { 31, 0.0, 1.75 }, { 31, 1.75, 1.0 }, { 34, 3.0, 1.0 }, { 31, 4.0, 2.5 }, { 29, 6.5, 0.75 }, { 26, 7.25, 0.75 } };
    return notes;
}

bbg::BassSettings settingsFrom(const juce::String& row)
{
    juce::ValueTree tree("BassSettings");
    for (const auto& pair : juce::StringArray::fromTokens(row, " ", ""))
        if (pair.containsChar('='))
            tree.setProperty(juce::Identifier(pair.upToFirstOccurrenceOf("=", false, false)),
                             pair.fromFirstOccurrenceOf("=", false, false).getDoubleValue(), nullptr);
    return bbg::bassSettingsFromTree(tree);
}

// one-pole-pair band energy share helper: RBJ-free, a simple 4th-order SVF low-pass at hz
struct Lp
{
    double a = 0.0, s1 = 0.0, s2 = 0.0, s3 = 0.0, s4 = 0.0;
    void set(double hz, double rate) { a = 1.0 - std::exp(-2.0 * 3.14159265358979 * hz / rate); }
    double run(double x)
    {
        s1 += a * (x - s1);
        s2 += a * (s1 - s2);
        s3 += a * (s2 - s3);
        s4 += a * (s3 - s4);
        return s4;
    }
};
} // namespace

int runAdvancedRender(const juce::File& spec, const juce::File& out, double bpm)
{
    using namespace bbg;
    out.createDirectory();
    juce::StringArray lines;
    lines.addLines(spec.loadFileAsString());
    juce::String table = "name\trms_db\tpeak_db\tsub\tlowmid\tmid\thigh\tside_mid_db\tside_mid_sub_db\tcorr\tpitch_cents\trt_factor\n";
    const double rate = 48000.0;
    const int block = 256;
    int rendered = 0;
    for (const auto& raw : lines)
    {
        const auto line = raw.trim();
        if (line.isEmpty() || line.startsWithChar('#'))
            continue;
        const auto fields = juce::StringArray::fromTokens(line, "|", "");
        if (fields.size() < 2)
            continue;
        const auto name = fields[0].trim();
        const auto settings = settingsFrom(fields[1]);
        const auto phrase = parsePhrase(fields.size() > 2 ? fields[2].trim() : juce::String());

        Bass808Processor bass;
        bass.setPlayConfigDetails(0, 2, rate, block);
        bass.writeSettings(settings);
        bass.prepareToPlay(rate, block);
        double endBeat = 0.0;
        for (const auto& n : phrase)
            endBeat = std::max(endBeat, n.start + n.length);
        const double samplesPerBeat = rate * 60.0 / bpm;
        const int total = static_cast<int>((endBeat + 1.0) * samplesPerBeat);
        juce::AudioBuffer<float> all(2, total);
        all.clear();
        juce::AudioBuffer<float> buffer(2, block);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int start = 0; start < total; start += block)
        {
            const int n = std::min(block, total - start);
            juce::MidiBuffer midi;
            for (const auto& note : phrase)
            {
                const int on = static_cast<int>(note.start * samplesPerBeat), off = static_cast<int>((note.start + note.length) * samplesPerBeat);
                if (off >= start && off < start + n)
                    midi.addEvent(juce::MidiMessage::noteOff(1, note.note), off - start);
                if (on >= start && on < start + n)
                    midi.addEvent(juce::MidiMessage::noteOn(1, note.note, 0.9f), on - start);
            }
            buffer.setSize(2, n, false, false, true);
            bass.processBlock(buffer, midi);
            for (int c = 0; c < 2; ++c)
                all.copyFrom(c, start, buffer, c, 0, n);
        }
        const double seconds = (juce::Time::getMillisecondCounterHiRes() - t0) * 0.001;

        // measurements
        Lp lp100, lp300, lp2k, sideLp;
        lp100.set(100.0, rate);
        lp300.set(300.0, rate);
        lp2k.set(2000.0, rate);
        sideLp.set(120.0, rate);
        Lp midLp;
        midLp.set(120.0, rate);
        double e = 0.0, peak = 0.0, eSub = 0.0, eLowMid = 0.0, eMid = 0.0, eHigh = 0.0, eSide = 0.0, eMidAll = 0.0, eSideSub = 0.0, eMidSub = 0.0;
        double lr = 0.0, ll = 0.0, rr = 0.0;
        for (int i = 0; i < total; ++i)
        {
            const double l = all.getSample(0, i), r = all.getSample(1, i);
            const double m = 0.5 * (l + r), s = 0.5 * (l - r);
            e += 0.5 * (l * l + r * r);
            peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
            const double a = lp100.run(m), b = lp300.run(m), c = lp2k.run(m);
            eSub += a * a;
            eLowMid += (b - a) * (b - a);
            eMid += (c - b) * (c - b);
            eHigh += (m - c) * (m - c);
            eSide += s * s;
            eMidAll += m * m;
            const double ss = sideLp.run(s), ms = midLp.run(m);
            eSideSub += ss * ss;
            eMidSub += ms * ms;
            lr += l * r;
            ll += l * l;
            rr += r * r;
        }
        const double bands = std::max(1.0e-12, eSub + eLowMid + eMid + eHigh);
        const auto db = [](double x) { return x > 1.0e-12 ? 10.0 * std::log10(x) : -120.0; };
        // pitch of the first note: zero crossings of the sub band between 30 % and 90 % of its length
        double cents = 0.0;
        {
            const auto& n0 = phrase.front();
            const int a = static_cast<int>((n0.start + 0.3 * n0.length) * samplesPerBeat);
            const int b = static_cast<int>((n0.start + 0.9 * n0.length) * samplesPerBeat);
            Lp f;
            f.set(1.6 * 440.0 * std::pow(2.0, (n0.note - 69) / 12.0), rate);
            double prev = 0.0, first = -1.0, last = -1.0;
            int crossings = 0;
            for (int i = 0; i < b && i < total; ++i)
            {
                const double x = f.run(0.5 * (all.getSample(0, i) + all.getSample(1, i)));
                if (i > a && prev < 0.0 && x >= 0.0)
                {
                    const double at = i - 1 + (-prev) / (x - prev);
                    if (first < 0.0)
                        first = at;
                    last = at;
                    ++crossings;
                }
                prev = x;
            }
            if (crossings > 2)
            {
                const double hz = (crossings - 1) * rate / (last - first);
                // the lowest sounding octave of the note: compare against the note and its lower octaves
                double best = 1.0e9;
                for (int oct = 0; oct <= 2; ++oct)
                {
                    const double want = 440.0 * std::pow(2.0, (n0.note - 12 * oct - 69) / 12.0);
                    const double c = 1200.0 * std::log2(hz / want);
                    if (std::abs(c) < std::abs(best))
                        best = c;
                }
                cents = best;
            }
        }
        const double corr = lr / std::sqrt(std::max(1.0e-12, ll * rr));
        table << name << "\t" << juce::String(db(e / total), 2) << "\t" << juce::String(20.0 * std::log10(std::max(1.0e-9, peak)), 2) << "\t"
              << juce::String(eSub / bands, 3) << "\t" << juce::String(eLowMid / bands, 3) << "\t" << juce::String(eMid / bands, 3) << "\t"
              << juce::String(eHigh / bands, 3) << "\t" << juce::String(db(eSide) - db(eMidAll), 1) << "\t"
              << juce::String(db(eSideSub) - db(eMidSub), 1) << "\t" << juce::String(corr, 3) << "\t" << juce::String(cents, 1) << "\t"
              << juce::String((total / rate) / std::max(1.0e-6, seconds), 1) << "\n";

        const auto file = out.getChildFile(name + ".wav");
        file.deleteFile();
        juce::WavAudioFormat wav;
        if (auto stream = std::unique_ptr<juce::OutputStream>(file.createOutputStream()))
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter>(wav.createWriterFor(stream.get(), rate, 2, 24, {}, 0)))
            {
                stream.release();
                writer->writeFromAudioSampleBuffer(all, 0, total);
            }
        ++rendered;
    }
    out.getChildFile("metrics.tsv").replaceWithText(table);
    std::printf("%s", table.toRawUTF8());
    std::printf("%d renders -> %s\n", rendered, out.getFullPathName().toRawUTF8());
    return rendered > 0 ? 0 : 1;
}
