#include <cstdio> // for printing to stdout

#include "Gamma/Analysis.h"
#include "Gamma/Effects.h"
#include "Gamma/Envelope.h"
#include "Gamma/Gamma.h"
#include "Gamma/Oscillator.h"
#include "Gamma/Types.h"
#include "al/app/al_App.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/scene/al_PolySynth.hpp"
#include "al/scene/al_SynthSequencer.hpp"
#include "al/ui/al_ControlGUI.hpp"
#include "al/ui/al_Parameter.hpp"

using namespace gam;
using namespace al;
using namespace std;

enum Instrument
{
  INSTR_SQUARE,
  INSTR_PL,
  NUM_INSTRUMENTS
};

class SquareWave : public SynthVoice
{
private:
    struct note
    {
        const float freq;
        float duration;
    };

public:
    // Unit generators
    gam::Pan<> mPan;
    gam::Sine<> mOsc1;
    gam::Sine<> mOsc3;
    gam::Sine<> mOsc5;
    gam::Sine<> mOsc7;

    gam::Env<3> mAmpEnv;

    // Initialize voice. This function will only be called once per voice when
    // it is created. Voices will be reused if they are idle.
    void init() override
    {
        // Intialize envelope
        mAmpEnv.curve(0); // make segments lines
        mAmpEnv.levels(0, 1, 1, 0);
        mAmpEnv.sustainPoint(2); // Make point 2 sustain until a release is issued

        createInternalTriggerParameter("amplitude", 0.8, 0.0, 1.0);
        createInternalTriggerParameter("frequency", 440, 20, 5000);
        createInternalTriggerParameter("attackTime", 0.1, 0.01, 3.0);
        createInternalTriggerParameter("releaseTime", 0.1, 0.1, 10.0);
        createInternalTriggerParameter("pan", 0.0, -1.0, 1.0);
    }

    // The audio processing function
    void onProcess(AudioIOData &io) override
    {
        // Get the values from the parameters and apply them to the corresponding
        // unit generators. You could place these lines in the onTrigger() function,
        // but placing them here allows for realtime prototyping on a running
        // voice, rather than having to trigger a new voice to hear the changes.
        // Parameters will update values once per audio callback because they
        // are outside the sample processing loop.
        float f = getInternalParameterValue("frequency");
        mOsc1.freq(f);
        mOsc3.freq(f * 3);
        mOsc5.freq(f * 5);
        mOsc7.freq(f * 7);

        float a = getInternalParameterValue("amplitude");
        mAmpEnv.lengths()[0] = getInternalParameterValue("attackTime");
        mAmpEnv.lengths()[2] = getInternalParameterValue("releaseTime");
        mPan.pos(getInternalParameterValue("pan"));
        while (io())
        {
            float s1 = mAmpEnv() * (mOsc1() * a +
                                    mOsc3() * (a / 3.0) +
                                    mOsc5() * (a / 5.0) +
                                    mOsc7() * (a / 7.0));

            float s2;
            mPan(s1, s1, s2);
            io.out(0) += s1;
            io.out(1) += s2;
        }
        // We need to let the synth know that this voice is done
        // by calling the free(). This takes the voice out of the
        // rendering chain
        if (mAmpEnv.done())
            free();
    }

    // The triggering functions just need to tell the envelope to start or release
    // The audio processing function checks when the envelope is done to remove
    // the voice from the processing chain.
    void onTriggerOn() override { mAmpEnv.reset(); }
    void onTriggerOff() override { mAmpEnv.release(); }
};

class PluckedString : public SynthVoice
{
private:
    struct note
    {
        const float freq;
        float duration;
    };

public:
    float mAmp;
    float mDur;
    float mPanRise;
    // unit generators: can be adjusted
    gam::Pan<> mPan;
    gam::NoiseWhite<> noise;
    gam::Decay<> env;
    gam::MovingAvg<> fil{2};
    gam::Delay<float, gam::ipl::Trunc> delay;
    gam::ADSR<> mAmpEnv;
    gam::EnvFollow<> mEnvFollow;
    gam::Env<2> mPanEnv;

    // Additional members
    Mesh mMesh;

    virtual void init()
    {
        mAmp = 1;
        mDur = 2;
        mAmpEnv.levels(0, 1, 1, 0);
        mPanEnv.curve(4);
        env.decay(0.1);
        delay.maxDelay(1. / 27.5);
        delay.delay(1. / 440.0);

        addDisc(mMesh, 1.0, 30);
        createInternalTriggerParameter("amplitude", 0.1, 0.0, 1.0);
        createInternalTriggerParameter("frequency", 60, 20, 5000);
        createInternalTriggerParameter("attackTime", 0.001, 0.001, 1.0);
        createInternalTriggerParameter("releaseTime", 3.0, 0.1, 10.0);
        createInternalTriggerParameter("sustain", 0.7, 0.0, 1.0);
        createInternalTriggerParameter("Pan1", 0.0, -1.0, 1.0);
        createInternalTriggerParameter("Pan2", 0.0, -1.0, 1.0);
        createInternalTriggerParameter("PanRise", 0.0, -1.0, 1.0); // range check
    }

    //    void reset(){ env.reset(); }

    float operator()()
    {
        return (*this)(noise() * env());
    }
    float operator()(float in)
    {
        return delay(
            fil(delay() + in));
    }

    virtual void onProcess(AudioIOData &io) override
    {

        while (io())
        {
            mPan.pos(mPanEnv());
            float s1 = (*this)() * mAmpEnv() * mAmp;
            float s2;
            mEnvFollow(s1);
            mPan(s1, s1, s2);
            io.out(0) += s1;
            io.out(1) += s2;
        }
        if (mAmpEnv.done() && (mEnvFollow.value() < 0.001))
            free();
    }

    virtual void onProcess(Graphics &g)
    {
        float frequency = getInternalParameterValue("frequency");
        float amplitude = getInternalParameterValue("amplitude");
        g.pushMatrix();
        g.translate(amplitude, amplitude, -4);
        //g.scale(frequency/2000, frequency/4000, 1);
        float scaling = 0.1;
        g.scale(scaling * frequency / 200, scaling * frequency / 400, scaling * 1);
        g.color(mEnvFollow.value(), frequency / 1000, mEnvFollow.value() * 10, 0.4);
        g.draw(mMesh);
        g.popMatrix();
    }

    virtual void onTriggerOn() override
    {
        updateFromParameters();
        mAmpEnv.reset();
        env.reset();
        delay.zero();
    }

    virtual void onTriggerOff() override
    {
        mAmpEnv.triggerRelease();
    }

    void updateFromParameters()
    {
        mPanEnv.levels(getInternalParameterValue("Pan1"),
                       getInternalParameterValue("Pan2"),
                       getInternalParameterValue("Pan1"));
        mPanRise = getInternalParameterValue("PanRise");
        delay.freq(getInternalParameterValue("frequency"));
        mAmp = getInternalParameterValue("amplitude");
        mAmpEnv.levels()[1] = 1.0;
        mAmpEnv.levels()[2] = getInternalParameterValue("sustain");
        mAmpEnv.lengths()[0] = getInternalParameterValue("attackTime");
        mAmpEnv.lengths()[3] = getInternalParameterValue("releaseTime");

        mPanEnv.lengths()[0] = mDur * (1 - mPanRise);
        mPanEnv.lengths()[1] = mDur * mPanRise;
    }
};

class MyApp : public App
{
public:
    SynthGUIManager<PluckedString> synthManager{"plunk"};
    //    ParameterMIDI parameterMIDI;

    virtual void onInit() override
    {
        imguiInit();
        navControl().active(false); // Disable navigation via keyboard, since we
                                    // will be using keyboard for note triggering
        // Set sampling rate for Gamma objects from app's audio
        gam::sampleRate(audioIO().framesPerSecond());
    }

    void onCreate() override
    {
        // Play example sequence. Comment this line to start from scratch
        //    synthManager.synthSequencer().playSequence("synth8.synthSequence");
        synthManager.synthRecorder().verbose(true);
    }

    void onSound(AudioIOData &io) override
    {
        synthManager.render(io); // Render audio
    }

    void onAnimate(double dt) override
    {
        imguiBeginFrame();
        synthManager.drawSynthControlPanel();
        imguiEndFrame();
    }

    void onDraw(Graphics &g) override
    {
        g.clear();
        synthManager.render(g);

        // Draw GUI
        imguiDraw();
    }

    // bool onKeyDown(Keyboard const &k) override
    // {
    //     if (ParameterGUI::usingKeyboard())
    //     { // Ignore keys if GUI is using them
    //         return true;
    //     }
    //     if (k.shift())
    //     {
    //         // If shift pressed then keyboard sets preset
    //         int presetNumber = asciiToIndex(k.key());
    //         synthManager.recallPreset(presetNumber);
    //     }
    //     else
    //     {
    //         // Otherwise trigger note for polyphonic synth
    //         int midiNote = asciiToMIDI(k.key());
    //         if (midiNote > 0)
    //         {
    //             synthManager.voice()->setInternalParameterValue(
    //                 "frequency", ::pow(2.f, (midiNote - 69.f) / 12.f) * 432.f);
    //             synthManager.triggerOn(midiNote);
    //         }
    //     }
    //     return true;
    // }

    // Whenever a key is pressed, this function is called
    bool onKeyDown(Keyboard const &k) override
    {
        if (ParameterGUI::usingKeyboard())
        { // Ignore keys if GUI is using
            // keyboard
            return true;
        }

        switch (k.key())
        {
        case 'b':
            std::cout << "b pressed!" << std::endl;
            playSequenceB();
            return false;

        case '1':
            std::cout << "1 pressed!" << std::endl;
            playSequenceB(0.6);
            return false;

        case '2':
            std::cout << "2 pressed!" << std::endl;
            playSequenceB(2);
            return false;

        case '3':
            std::cout << "3 pressed!" << std::endl;
            playSequenceB(0.2);
            return false;

        case '4':
            std::cout << "4 pressed!" << std::endl;
            playSequenceB(0.15);
            return false;
        }

        return true;
    }

    bool onKeyUp(Keyboard const &k) override
    {
        int midiNote = asciiToMIDI(k.key());
        if (midiNote > 0)
        {
            synthManager.triggerOff(midiNote);
        }
        return true;
    }

    void onExit() override { imguiShutdown(); }

    // void playNote(float freq, float time, float duration, float releaseTime = 1.0, float attackTime = 0.1)
    // {
    //     auto *voice = synthManager.synth().getVoice<PluckedString>();
        
    //     // amp, freq, attack, release, pan
    //     voice->setInternalParameterValue("amplitude", 0.2);
    //     voice->setInternalParameterValue("frequency", freq);
    //     voice->setInternalParameterValue("attackTime", attackTime);
    //     voice->setInternalParameterValue("releaseTime", releaseTime);
    //     voice->setInternalParameterValue("pan", 0.0);
    //     // voice->setTriggerParams({0.2, freq, attackTime, releaseTime, 0.0});
    //     synthManager.synthSequencer().addVoiceFromNow(voice, time, duration);
    // }

    void playNote(float freq, float time, float duration, Instrument instrument, float amp = 0.2, float attack = 0.1, float decay = 0.1)
    {
        SynthVoice *voice;
        switch (instrument)
        {

        case INSTR_SQUARE:
            voice = synthManager.synth().getVoice<SquareWave>();
            voice->setInternalParameterValue("amplitude", amp);
            voice->setInternalParameterValue("frequency", freq);
            voice->setInternalParameterValue("attackTime", 0.1);
            voice->setInternalParameterValue("releaseTime", 0.1);
            voice->setInternalParameterValue("pan", -1.0);
            break;

        case INSTR_PL:
            voice = synthManager.synth().getVoice<PluckedString>();
            voice->setInternalParameterValue("amplitude", 0.2);
            voice->setInternalParameterValue("frequency", freq);
            voice->setInternalParameterValue("attackTime", 0.1);
            voice->setInternalParameterValue("releaseTime", 0.1);
            voice->setInternalParameterValue("pan", 0.0);

            break;
        default:
            voice = nullptr;
            break;
        }
        synthManager.synthSequencer().addVoiceFromNow(voice, time, duration);
    }

    void playSequenceB(float offset = 1.0)
    {
        // what the pipa says

        // left hand
        const float D2 = 73.416;
        const float F2 = 87.307;
        const float C3 = 130.81;
        const float A3 = 220.00;

        const float A2 = 110.00;
        const float D3 = 146.83;
        const float E3 = 164.81;
        const float F3 = 174.61;
        const float C4 = 261.63;
        const float B3 = 246.94;

        // right hand
        const float B5 = 987.77;
        const float A5 = 880.00;
        const float G5 = 783.99;
        const float E5 = 659.26;
        const float D5 = 587.33;
        const float C5 = 523.25;
        const float B4 = 493.88;
        const float A4 = 440.00;
        const float G4 = 392.00;

        int section = 0;

        // bool instrument = true;
        // Instrument instrument = INSTR_SQUARE;

        // preface
        playNote (A2 * offset, section + 1, 0.25, INSTR_SQUARE);
        playNote(E3 * offset, section + 1.25, 0.25, INSTR_SQUARE);
        playNote(C4 * offset, section + 1.50, 1.50, INSTR_SQUARE);
        playNote(A2 * offset, section + 3, 0.25, INSTR_SQUARE);
        playNote(D3 * offset, section + 3.25, 0.25, INSTR_SQUARE);
        playNote(B3 * offset, section + 3.50, 1.50, INSTR_SQUARE);

        section += 4;

        playNote(A2 * offset, section + 1, 0.25, INSTR_SQUARE);
        playNote(E3 * offset, section + 1.25, 0.25, INSTR_SQUARE);
        playNote(C4 * offset, section + 1.50, 1.50, INSTR_SQUARE);
        playNote(A2 * offset, section + 3, 0.25, INSTR_SQUARE);
        playNote(D3 * offset, section + 3.25, 0.25, INSTR_SQUARE);
        playNote(B3 * offset, section + 3.50, 1.50, INSTR_SQUARE);
        section += 4;

        // section 1

        // left hand
        playNote(A2 * offset, section + 1, 0.25, INSTR_SQUARE);
        playNote(E3 * offset, section + 1.25, 0.25, INSTR_SQUARE);
        playNote(C4 * offset, section + 1.50, 1.50, INSTR_SQUARE);
        playNote(A2 * offset, section + 3, 0.25, INSTR_SQUARE);
        playNote(D3 * offset, section + 3.25, 0.25, INSTR_SQUARE);
        playNote(B3 * offset, section + 3.50, 1.50, INSTR_SQUARE);

        // right hand
        playNote(A5 * offset, section + 1, 0.75, INSTR_PL);
        playNote(G5 * offset, section + 1.75, 0.25, INSTR_PL);
        playNote(A5 * offset, section + 2, 1.75, INSTR_PL);
        playNote(G5 * offset, section + 3.75, 0.25, INSTR_PL);

        playNote(A5 * offset, section + 4.00, 0.25, INSTR_PL);
        playNote(B5 * offset, section + 4.25, 0.25, INSTR_PL);
        playNote(A5 * offset, section + 4.50, 0.25, INSTR_PL);
        playNote(G5 * offset, section + 4.75, 0.25, INSTR_PL); // end with 5

        section += 4;

        // section 2

        // left hand
        playNote(A2 * offset, section + 1, 0.25, INSTR_SQUARE);
        playNote(E3 * offset, section + 1.25, 0.25, INSTR_SQUARE);
        playNote(C4 * offset, section + 1.50, 1.50, INSTR_SQUARE);
        playNote(A2 * offset, section + 3, 0.25, INSTR_SQUARE);
        playNote(D3 * offset, section + 3.25, 0.25, INSTR_SQUARE);
        playNote(B3 * offset, section + 3.50, 1.50, INSTR_SQUARE);

        // right hand
        playNote(E5 * offset, section + 1, 0.75, INSTR_PL);
        playNote(D5 * offset, section + 1.75, 0.25, INSTR_PL);
        playNote(E5 * offset, section + 2, 2, INSTR_PL);

        playNote(D5 * offset, section + 4.00, 0.25, INSTR_PL);
        playNote(E5 * offset, section + 4.25, 0.25, INSTR_PL);
        playNote(G5 * offset, section + 4.50, 0.25, INSTR_PL);
        playNote(E5 * offset, section + 4.75, 0.25, INSTR_PL);

        section += 4;

        // section 3

        // left hand
        playNote(F2 * offset, section + 1, 0.25, INSTR_SQUARE);
        playNote(C3 * offset, section + 1.25, 0.25, INSTR_SQUARE);
        playNote(A3 * offset, section + 1.50, 1.50, INSTR_SQUARE);
        playNote(D2 * offset, section + 3, 0.25, INSTR_SQUARE);
        playNote(A2 * offset, section + 3.25, 0.25, INSTR_SQUARE);
        playNote(F3 * offset, section + 3.50, 1.50, INSTR_SQUARE);

        // right hand
        playNote(D5 * offset, section + 1, 0.75, INSTR_PL);
        playNote(E5 * offset, section + 1.75, 0.25, INSTR_PL);
        playNote(C5 * offset, section + 2.00, 2, INSTR_PL);

        playNote(B4 * offset, section + 4.00, 0.25, INSTR_PL);
        playNote(C5 * offset, section + 4.25, 0.25, INSTR_PL);
        playNote(B4 * offset, section + 4.50, 0.25, INSTR_PL);
        playNote(G4 * offset, section + 4.75, 0.25, INSTR_PL);

        section += 4;

        // section 4

        // left hand
        playNote(A2 * offset, section + 1, 0.25, INSTR_SQUARE);
        playNote(E3 * offset, section + 1.25, 0.25, INSTR_SQUARE);
        playNote(C4 * offset, section + 1.50, 1.50, INSTR_SQUARE);
        playNote(A2 * offset, section + 3, 0.25, INSTR_SQUARE);
        playNote(D3 * offset, section + 3.25, 0.25, INSTR_SQUARE);
        playNote(B3 * offset, section + 3.50, 1.50, INSTR_SQUARE);

        // right hand
        playNote(A4 * offset, section + 1, 3, INSTR_PL);
    }
};

int main()
{
    MyApp app;

    // Set up audio
    app.configureAudio(48000., 512, 2, 0);

    app.start();
}
