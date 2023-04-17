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

// using namespace gam;
using namespace al;

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
    case 'a':
      std::cout << "a pressed!" << std::endl;
      playSequenceA();
      return false;

    case 'b':
      std::cout << "b pressed!" << std::endl;
      playSequenceB();
      return false;

    case '1':
      std::cout << "1 pressed!" << std::endl;
      playSequenceB(1.0);
      return false;
    
    case '2':
      std::cout << "2 pressed!" << std::endl;
      playSequenceB(pow(2.0, 2.0/12.0));
      return false;

    case '3':
      std::cout << "3 pressed!" << std::endl;
      playSequenceB(pow(2.0, 3.0/12.0));
      return false;

    case '4':
      std::cout << "4 pressed!" << std::endl;
      playSequenceB(pow(2.0, 4.0/12.0));
      return false;

    case '5':
      std::cout << "5 pressed!" << std::endl;
      playSequenceB(pow(2.0, 5.0/12.0));
      return false;

    case '6':
      std::cout << "6 pressed!" << std::endl;
      playSequenceB(pow(2.0, 6.0/12.0));
      return false;

    case '7':
      std::cout << "7 pressed!" << std::endl;
      playSequenceB(pow(2.0, 7.0/12.0));
      return false;

    case '8':
      std::cout << "8 pressed!" << std::endl;
      playSequenceB(pow(2.0, 8.0/12.0));
      return false;

    case '9':
      std::cout << "9 pressed!" << std::endl;
      playSequenceB(pow(2.0, 9.0/12.0));
      return false;

    }

    return true;
  }

  // Whenever a key is released this function is called
  bool
  onKeyUp(Keyboard const &k) override
  {
    int midiNote = asciiToMIDI(k.key());
    if (midiNote > 0)
    {
      synthManager.triggerOff(midiNote);
    }
    return true;
  }

  void onExit() override { imguiShutdown(); }

  // New code: a function to play a note A

  void playNote(float freq, float time, float duration = 0.5, float amp = 0.2, float attack = 0.1, float decay = 0.1)
  {
    auto *voice = synthManager.synth().getVoice<SquareWave>();
    // amp, freq, attack, release, pan
    voice->setTriggerParams({amp, freq, 0.1, 0.1, 0.0});
    synthManager.synthSequencer().addVoiceFromNow(voice, time, duration);
  }

  void playSequenceA()
  {
    playNote(110.0, 0, 0.5, 0.1);
    playNote(220.0, 1, 0.5, 0.2);
    playNote(330.0, 2, 0.5, 0.4);
    playNote(440.0, 3, 0.5, 0.2);
    playNote(550.0, 4, 0.5, 0.1);
  }

  void playSequenceB(float offset = 1.0)
  {
    const float C4 = 261.6;
    const float D4 = 293.7;
    const float E4 = 329.6;
    const float F4 = 349.2;
    const float G4 = 392.0;
    const float A4 = 440.0;

    const float G3 = G4/2.0;

    playNote(C4 * offset, 0, 0.5, 0.1);
    playNote(D4 * offset, 1, 0.5, 0.2);
    playNote(E4 * offset, 2, 0.5, 0.3);
    playNote(C4 * offset, 3, 0.5, 0.2);
    
    playNote(C4 * offset, 4, 0.5, 0.1);
    playNote(D4 * offset, 5, 0.5, 0.2);
    playNote(E4 * offset, 6, 0.5, 0.3);
    playNote(C4 * offset, 7, 0.5, 0.1);

    playNote(E4 * offset, 8, 0.5, 0.3);
    playNote(F4 * offset, 9, 0.5, 0.4);
    playNote(G4 * offset, 10, 1.0, 0.5);

    playNote(E4 * offset, 12, 0.5, 0.1);
    playNote(F4 * offset, 13, 0.5, 0.2);
    playNote(G4 * offset, 14, 1.0, 0.3);
    
    playNote(G4 * offset, 16, 0.25, 0.2);
    playNote(A4 * offset, 16.5, 0.25, 0.3);
    playNote(G4 * offset, 17, 0.25, 0.4);
    playNote(F4 * offset, 17.5, 0.25, 0.45);
    playNote(E4 * offset, 18, 0.5, 0.5);
    playNote(C4 * offset, 19, 0.5, 0.25);
    
    playNote(G4 * offset, 20, 0.25, 0.1);
    playNote(A4 * offset, 20.5, 0.25, 0.2);
    playNote(G4 * offset, 21, 0.25, 0.25);
    playNote(F4 * offset, 21.5, 0.25, 0.2);
    playNote(E4 * offset, 22, 0.5, 0.1);
    playNote(C4 * offset, 23, 0.5, 0.1);
    
    playNote(C4 * offset, 24, 0.5, 0.2);
    playNote(G3 * offset, 25, 0.5, 0.1);
    playNote(C4 * offset, 26, 1.0, 0.05);
   
    playNote(C4 * offset, 28, 0.5, 0.15);
    playNote(G3 * offset, 29, 0.5, 0.05);
    playNote(C4 * offset, 30, 1.0, 0.03);
   
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
