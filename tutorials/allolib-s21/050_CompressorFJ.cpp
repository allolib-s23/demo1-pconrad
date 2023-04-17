#include <cstdio> // for printing to stdout

#include "Gamma/Analysis.h"
#include "Gamma/Effects.h"
#include "Gamma/Envelope.h"
#include "Gamma/Oscillator.h"

#include "al/app/al_App.hpp"
#include "al/graphics/al_Shapes.hpp"
#include "al/scene/al_PolySynth.hpp"
#include "al/scene/al_SynthSequencer.hpp"
#include "al/ui/al_ControlGUI.hpp"
#include "al/ui/al_Parameter.hpp"

#include <cassert>
#include <vector>
#include <cmath> // std::abs

#include "SimpleCompressor/src/LookAheadGainReduction.h"
#include "SimpleCompressor/src/LookAheadGainReduction.cpp"
#include "SimpleCompressor/src/GainReductionComputer.h"
#include "SimpleCompressor/src/GainReductionComputer.cpp"

// using namespace gam;
using namespace al;

const int BLOCK_SIZE = 128;

float linearToDecibels(float linear)
{
  return 20.f * std::log10(std::abs(linear));
}

float decibelsToLinear(float decibels)
{
  return std::pow(10.f, decibels / 20.f);
}

class CompressorStats
{
public:
  float pre_peak;
  float duck;
  float post_peak;
  CompressorStats(float pre_peak, float duck, float post_peak)
  {
    this->set(pre_peak, duck, post_peak);
  };
  void set(float pre_peak, float duck, float post_peak)
  {
    this->pre_peak = pre_peak;
    this->duck = duck;
    this->post_peak = post_peak;
  };
};

bool approxEqual(float l, float r)
{
  return std::abs(l - r) < 0.0001f;
}

bool operator==(const CompressorStats &l, const CompressorStats &r)
{
  return approxEqual(l.pre_peak, r.pre_peak) && approxEqual(l.duck, r.duck) && approxEqual(l.post_peak, r.post_peak);
}

template <int block_size>
class CompressorPlugin
{
public:
  bool useLookAhead = false;
  bool debug = true;

  CompressorStats previousStats = CompressorStats(0.0f, 1.0f, 0.0f);

  CompressorPlugin()
  {
    gain.prepare(48000.);
    gain.setThreshold(-5.f);
    gain.setRatio(100.f);
    gain.setKnee(20.f);
    gain.setAttackTime(0.0025f);

    lookahead.setDelayTime(0.005f);
    lookahead.prepare(48000., 2 * block_size);
  };

  AudioIOData &operator()(AudioIOData &io)
  {
    // TODO get look-ahead working
    io.frame(0);
    for (int i = 0; io() && i < BLOCK_SIZE; i++)
    {
      sidechain_buf[i] = std::max(std::abs(io.out(0)), std::abs(io.out(1)));
    }

    if (useLookAhead)
    {
      gain.computeGainInDecibelsFromSidechainSignal(sidechain_buf, gain_buf, block_size);
    }
    else
    {
      gain.computeLinearGainFromSidechainSignal(sidechain_buf, gain_buf, block_size);
    }

    if (useLookAhead)
    {
      lookahead.pushSamples(gain_buf, block_size);
      lookahead.process();
      lookahead.readSamples(look_buf, block_size);

      for (int i = 0; i < block_size; i++)
      {
        gain_buf[i] = decibelsToLinear(look_buf[i]);
      }
    }

    float pre_peak = 0;
    float duck = 1;
    float post_peak = 0;
    io.frame(0);
    for (int i = 0; io() && i < BLOCK_SIZE; i++)
    {
      pre_peak = std::max(pre_peak, std::abs(io.out(0)));
      pre_peak = std::max(pre_peak, std::abs(io.out(1)));
      duck = std::min(duck, gain_buf[i]);
      io.out(0) *= gain_buf[i];
      io.out(1) *= gain_buf[i];
      post_peak = std::max(post_peak, std::abs(io.out(0)));
      post_peak = std::max(post_peak, std::abs(io.out(1)));
    }

    if (debug)
    {
      CompressorStats currentStats(pre_peak, duck, post_peak);
      if (!(currentStats == previousStats))
      {
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "pre_peak:  " << std::setw(10) << linearToDecibels(pre_peak) << " dB  "
                  << "compress:  " << std::setw(10) << linearToDecibels(duck) << " dB  "
                  << "post_peak: " << std::setw(10) << linearToDecibels(post_peak) << " dB" << std::endl;
        previousStats = currentStats;
      }
    }

    return io;
  }

private:
  GainReductionComputer gain;
  LookAheadGainReduction lookahead;

  float sidechain_buf[block_size];
  float gain_buf[block_size];
  float look_buf[block_size];
};

enum Instrument
{
  INSTR_SQUARE,
  INSTR_FM,
  NUM_INSTRUMENTS
};

const float C4 = 261.6;
const float D4 = 293.7;
const float E4 = 329.6;
const float F4 = 349.2;
const float G4 = 392.0;
const float A4 = 440.0;

const float G3 = G4 / 2.0;

class TimeSignature
{
private:
  int upper;
  int lower;

public:
  TimeSignature()
  {
    this->upper = 4;
    this->lower = 4;
  }
};

class Note
{
private:
  float freq;
  float time;
  float duration;
  float amp;
  float attack;
  float decay;

public:
  Note()
  {
    this->freq = 440.0;
    this->time = 0;
    this->duration = 0.5;
    this->amp = 0.2;
    this->attack = 0.05;
    this->decay = 0.05;
  }
  Note(float freq,
       float time = 0.0f,
       float duration = 0.5f,
       float amp = 0.2f,
       float attack = 0.05f,
       float decay = 0.05f)
  {
    this->freq = freq;
    this->time = time;
    this->duration = duration;
    this->amp = amp;
    this->attack = attack;
    this->decay = decay;
  }
  // Return an identical note, but offset by the
  // number of beats indicated by beatOffset,
  // and with amplitude multiplied by ampMult
  Note(const Note &n, float beatOffset, float ampMult = 1.0f)
  {
    this->freq = n.freq;
    this->time = n.time + beatOffset;
    this->duration = n.duration;
    this->amp = n.amp * ampMult;
    this->attack = n.attack;
    this->decay = n.decay;
  }
  Note(const Note &n)
  {
    this->freq = n.freq;
    this->time = n.time;
    this->duration = n.duration;
    this->amp = n.amp;
    this->attack = n.attack;
    this->decay = n.decay;
  }
  float getFreq() { return this->freq; }
  float getTime() { return this->time; }
  float getDuration() { return this->duration; }
  float getAmp() { return this->amp; }
  float getAttack() { return this->attack; }
  float getDecay() { return this->decay; }
};

class Sequence
{
private:
  TimeSignature ts;
  std::vector<Note> notes;

public:
  Sequence(TimeSignature ts)
  {
    this->ts = ts;
  }

  void add(Note n)
  {
    notes.push_back(n);
  }

  // Add notes from the source sequence s,
  // but starting on the beat indicated by startBeat

  void addSequence(Sequence *s, float startBeat, float ampMult = 1.0)
  {
    for (auto &note : *(s->getNotes()))
      add(Note(note, startBeat, ampMult));
  }

  std::vector<Note> *getNotes()
  {
    return &notes;
  }
};

// In a class that inherits from SynthVoice you will
// define the synth's voice parameters and the sound and graphic generation
// processes in the onProcess() functions.

class FM : public SynthVoice
{
public:
  // Unit generators
  gam::Pan<> mPan;
  gam::ADSR<> mAmpEnv;
  gam::ADSR<> mModEnv;
  gam::EnvFollow<> mEnvFollow;

  gam::Sine<> car, mod; // carrier, modulator sine oscillators

  // Additional members
  Mesh mMesh;

  void init() override
  {
    //      mAmpEnv.curve(0); // linear segments
    mAmpEnv.levels(0, 1, 1, 0);

    // We have the mesh be a sphere
    addDisc(mMesh, 1.0, 30);

    createInternalTriggerParameter("amplitude", 0.5, 0.0, 1.0);
    createInternalTriggerParameter("freq", 440, 10, 4000.0);
    createInternalTriggerParameter("attackTime", 0.1, 0.01, 3.0);
    createInternalTriggerParameter("releaseTime", 0.1, 0.1, 10.0);
    createInternalTriggerParameter("pan", 0.0, -1.0, 1.0);

    // FM index
    createInternalTriggerParameter("idx1", 0.01, 0.0, 10.0);
    createInternalTriggerParameter("idx2", 7, 0.0, 10.0);
    createInternalTriggerParameter("idx3", 5, 0.0, 10.0);

    createInternalTriggerParameter("carMul", 1, 0.0, 20.0);
    createInternalTriggerParameter("modMul", 1.0007, 0.0, 20.0);
    createInternalTriggerParameter("sustain", 0.75, 0.1, 1.0); // Unused
  }

  //
  void onProcess(AudioIOData &io) override
  {
    float modFreq =
        getInternalParameterValue("freq") * getInternalParameterValue("modMul");
    mod.freq(modFreq);
    float carBaseFreq =
        getInternalParameterValue("freq") * getInternalParameterValue("carMul");
    float modScale =
        getInternalParameterValue("freq") * getInternalParameterValue("modMul");
    float amp = getInternalParameterValue("amplitude");
    while (io())
    {
      car.freq(carBaseFreq + mod() * mModEnv() * modScale);
      float s1 = car() * mAmpEnv() * amp;
      float s2;
      mEnvFollow(s1);
      mPan(s1, s1, s2);
      io.out(0) += s1;
      io.out(1) += s2;
    }
    if (mAmpEnv.done() && (mEnvFollow.value() < 0.001))
      free();
  }

  void onProcess(Graphics &g) override
  {
    g.pushMatrix();
    g.translate(getInternalParameterValue("freq") / 300 - 2,
                getInternalParameterValue("modAmt") / 25 - 1, -4);
    float scaling = getInternalParameterValue("amplitude") * 1;
    g.scale(scaling, scaling, scaling * 1);
    g.color(HSV(getInternalParameterValue("modMul") / 20, 1,
                mEnvFollow.value() * 10));
    g.draw(mMesh);
    g.popMatrix();
  }

  void onTriggerOn() override
  {
    mModEnv.levels()[0] = getInternalParameterValue("idx1");
    mModEnv.levels()[1] = getInternalParameterValue("idx2");
    mModEnv.levels()[2] = getInternalParameterValue("idx2");
    mModEnv.levels()[3] = getInternalParameterValue("idx3");

    mAmpEnv.lengths()[0] = getInternalParameterValue("attackTime");
    mModEnv.lengths()[0] = getInternalParameterValue("attackTime");

    mAmpEnv.lengths()[1] = 0.001;
    mModEnv.lengths()[1] = 0.001;

    mAmpEnv.lengths()[2] = getInternalParameterValue("releaseTime");
    mModEnv.lengths()[2] = getInternalParameterValue("releaseTime");
    mPan.pos(getInternalParameterValue("pan"));

    //        mModEnv.lengths()[1] = mAmpEnv.lengths()[1];

    mAmpEnv.reset();
    mModEnv.reset();
  }
  void onTriggerOff() override
  {
    mAmpEnv.triggerRelease();
    mModEnv.triggerRelease();
  }
};

// This example shows how to use SynthVoice and SynthManagerto create an audio
// visual synthesizer. In a class that inherits from SynthVoice you will
// define the synth's voice parameters and the sound and graphic generation
// processes in the onProcess() functions.

class SquareWave : public SynthVoice
{
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

// We make an app.
class MyApp : public App
{
public:
  // GUI manager for SquareWave voices
  // The name provided determines the name of the directory
  // where the presets and sequences are stored
  SynthGUIManager<SquareWave> synthManager{"SquareWave"};

  CompressorPlugin<BLOCK_SIZE> compressor;

  bool useCompressor = true;

  // This function is called right after the window is created
  // It provides a grphics context to initialize ParameterGUI
  // It's also a good place to put things that should
  // happen once at startup.
  void onCreate() override
  {
    navControl().active(false); // Disable navigation via keyboard, since we
                                // will be using keyboard for note triggering

    // Set sampling rate for Gamma objects from app's audio
    gam::sampleRate(audioIO().framesPerSecond());

    imguiInit();

    // Play example sequence. Comment this line to start from scratch
    // synthManager.synthSequencer().playSequence("synth1.synthSequence");
    synthManager.synthRecorder().verbose(true);
  }

  // The audio callback function. Called when audio hardware requires data
  void onSound(AudioIOData &io) override
  {
    synthManager.render(io); // Render audio
    if (useCompressor)
      compressor(io);
  }

  void onAnimate(double dt) override
  {
    // The GUI is prepared here
    imguiBeginFrame();
    // Draw a window that contains the synth control panel
    synthManager.drawSynthControlPanel();
    imguiEndFrame();
  }

  // The graphics callback function.
  void onDraw(Graphics &g) override
  {
    g.clear();
    // Render the synth's graphics
    synthManager.render(g);

    // GUI is drawn here
    imguiDraw();
  }

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

    case '=':
      std::cout << "= pressed!" << std::endl;
      useCompressor = !useCompressor;
      std::cout << "useCompressor=" << useCompressor << std::endl;
      return false;

    case '-':
      std::cout << "- pressed!" << std::endl;
      compressor.debug = !compressor.debug;
      std::cout << "compressor.debug=" << compressor.debug << std::endl;
      return false;

    case '1':
      std::cout << "1 pressed!" << std::endl;
      playSequenceFJ(1.0, 120);
      return false;

    case '2':
      std::cout << "2 pressed!" << std::endl;
      playSequenceFJ(1.0, 60);
      return false;

    case '3':
      std::cout << "3 pressed!" << std::endl;
      playSequenceFJ(1.0, 240);
      return false;

    case '4':
      std::cout << "4 pressed!" << std::endl;
      playSequenceFJ(1.0, 30);
      return false;

    case 'q':
      std::cout << "q pressed!" << std::endl;
      playSequenceFJ(pow(2.0, 7.0 / 12.0), 120);
      return false;

    case 'w':
      std::cout << "w pressed!" << std::endl;
      playSequenceFJ(pow(2.0, 7.0 / 12.0), 60);
      return false;

    case 'e':
      std::cout << "e pressed!" << std::endl;
      playSequenceFJ(pow(2.0, 7.0 / 12.0), 240);
      return false;

    case 'r':
      std::cout << "r pressed!" << std::endl;
      playSequenceFJ(pow(2.0, 7.0 / 12.0), 30);
      return false;

    case 'a':
      std::cout << "q pressed!" << std::endl;
      playSequenceFJ(1.0, 120, INSTR_FM);
      return false;

    case 's':
      std::cout << "w pressed!" << std::endl;
      playSequenceFJ(1.0, 60, INSTR_FM);
      return false;

    case 'd':
      std::cout << "e pressed!" << std::endl;
      playSequenceFJ(1.0, 240, INSTR_FM);
      return false;

    case 'f':
      std::cout << "r pressed!" << std::endl;
      playSequenceFJ(1.0, 30, INSTR_FM);
      return false;
    }

    return true;
  }

  // Whenever a key is released this function is called
  bool
  onKeyUp(Keyboard const &k) override
  {
    // int midiNote = asciiToMIDI(k.key());
    // if (midiNote > 0)
    // {
    //   synthManager.triggerOff(midiNote);
    // }
    // return true;
    return true;
  }

  void onExit() override { imguiShutdown(); }

  // New code: a function to play a note A

  void playNote(float freq, float time, float duration = 0.5, float amp = 0.2, float attack = 0.1, float decay = 0.1, Instrument instrument = INSTR_SQUARE)
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

    case INSTR_FM:
      voice = synthManager.synth().getVoice<FM>();

      voice->setInternalParameterValue("amplitude", amp);
      voice->setInternalParameterValue("freq", freq);
      voice->setInternalParameterValue("attackTime", 0.1);
      voice->setInternalParameterValue("releaseTime", 0.1);
      voice->setInternalParameterValue("pan", 1.0);

      break;
    default:
      voice = nullptr;
      break;
    }
    synthManager.synthSequencer().addVoiceFromNow(voice, time, duration);
  }

  Sequence *sequenceFJ(float offset = 1.0)
  {
    TimeSignature t;
    Sequence *result = new Sequence(t);

    result->addSequence(sequenceFJPhrase1(offset), 0);
    result->addSequence(sequenceFJPhrase1(offset), 4, 0.2);

    result->addSequence(sequenceFJPhrase2(offset), 8, 0.5);
    result->addSequence(sequenceFJPhrase2(offset), 12, 1.0);

    result->addSequence(sequenceFJPhrase3(offset), 16, 1.0);
    result->addSequence(sequenceFJPhrase3(offset), 20, 0.5);

    result->addSequence(sequenceFJPhrase4(offset), 24, 1.0);
    result->addSequence(sequenceFJPhrase4(offset), 28, 0.5);

    return result;
  }

  Sequence *sequenceFJPhrase1(float offset = 1.0)
  {
    TimeSignature t;
    Sequence *result = new Sequence(t);

    result->add(Note(C4 * offset, 0, 0.5, 0.05));
    result->add(Note(D4 * offset, 1, 0.5, 0.1));
    result->add(Note(E4 * offset, 2, 0.5, 0.2));
    result->add(Note(C4 * offset, 3, 0.5, 0.05));

    return result;
  }

  Sequence *sequenceFJPhrase2(float offset = 1.0)
  {
    TimeSignature t;
    Sequence *result = new Sequence(t);

    result->add(Note(E4 * offset, 0, 0.5, 0.1));
    result->add(Note(F4 * offset, 1, 0.5, 0.2));
    result->add(Note(G4 * offset, 2, 1.0, 0.25));

    return result;
  }

  Sequence *sequenceFJPhrase3(float offset = 1.0)
  {
    TimeSignature t;
    Sequence *result = new Sequence(t);

    result->add(Note(G4 * offset, 0, 0.25, 0.2));
    result->add(Note(A4 * offset, 0.5, 0.25, 0.24));
    result->add(Note(G4 * offset, 1, 0.25, 0.28));
    result->add(Note(F4 * offset, 1.5, 0.25, 0.32));
    result->add(Note(E4 * offset, 2, 0.5, 0.36));
    result->add(Note(C4 * offset, 3, 0.5, 0.24));

    return result;
  }

  Sequence *sequenceFJPhrase4(float offset = 1.0)
  {
    TimeSignature t;
    Sequence *result = new Sequence(t);

    result->add(Note(C4 * offset, 0, 0.5, 0.2));
    result->add(Note(G3 * offset, 1, 0.5, 0.1));
    result->add(Note(C4 * offset, 2, 1.0, 0.05));

    return result;
  }

  // bpm is beats per minute

  void playSequence(Sequence *s, float bpm, Instrument instrument = INSTR_SQUARE)
  {
    float secondsPerBeat = 60.0f / bpm;

    std::vector<Note> *notes = s->getNotes();

    for (auto &note : *notes)
    {
      playNote(
          note.getFreq(),
          note.getTime() * secondsPerBeat,
          note.getDuration() * secondsPerBeat,
          note.getAmp(),
          note.getAttack(),
          note.getDecay(),
          instrument);
    }
  }

  void playSequenceFJ(float offset = 1.0, float bpm = 120.0, Instrument instrument = INSTR_SQUARE)
  {
    std::cout << "playSequenceFJ: offset=" << offset << " bpm=" << bpm << std::endl;
    Sequence *fjSequence = sequenceFJ(offset);
    playSequence(fjSequence, bpm, instrument);
  }
};

int main()
{
  // Create app instance
  MyApp app;

  // Set up audio
  app.configureAudio(48000., 512, 2, 0);

  app.start();

  return 0;
}
