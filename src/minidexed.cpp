//
// minidexed.cpp
//
// MiniDexed - Dexed FM synthesizer for bare metal Raspberry Pi
// Copyright (C) 2022  The MiniDexed Team
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "minidexed.h"
#include <circle/logger.h>
#include <circle/memory.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/sound/i2ssoundbasedevice.h>
#include <circle/sound/hdmisoundbasedevice.h>
#include <circle/gpiopin.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

LOGMODULE ("minidexed");

CMiniDexed::CMiniDexed (CConfig *pConfig, CInterruptSystem *pInterrupt,
			CGPIOManager *pGPIOManager, CI2CMaster *pI2CMaster, FATFS *pFileSystem)
:
#ifdef ARM_ALLOW_MULTI_CORE
	CMultiCoreSupport (CMemorySystem::Get ()),
#endif
	m_pConfig (pConfig),
	m_UI (this, pGPIOManager, pI2CMaster, pConfig),
	m_PerformanceConfig (pFileSystem),
	m_PCKeyboard (this, pConfig, &m_UI),
	m_SerialMIDI (this, pInterrupt, pConfig, &m_UI),
	m_bUseSerial (false),
	m_pSoundDevice (0),
	m_bChannelsSwapped (pConfig->GetChannelsSwapped ()),
#ifdef ARM_ALLOW_MULTI_CORE
	m_nActiveTGsLog2 (0),
#endif
	m_GetChunkTimer ("GetChunk", 1000000U * pConfig->GetChunkSize ()/2 / pConfig->GetSampleRate ()),
	m_bProfileEnabled (m_pConfig->GetProfileEnabled ()),
	m_bSavePerformance (false),
	m_bSavePerformanceNewFile (false),
	m_bSetNewPerformance (false),
	m_bDeletePerformance (false),
	m_bLoadPerformanceBusy(false)
{
	assert (m_pConfig);

	for (unsigned i = 0; i < CConfig::ToneGenerators; i++)
	{
		m_nVoiceBankID[i] = 0;
		m_nProgram[i] = 0;
		m_nVolume[i] = 100;
		m_nPan[i] = 64;
		m_nMasterTune[i] = 0;
		m_nCutoff[i] = 99;
		m_nResonance[i] = 0;
		m_nMIDIChannel[i] = CMIDIDevice::Disabled;
		m_nPitchBendRange[i] = 2;
		m_nPitchBendStep[i] = 0;
		m_nPortamentoMode[i] = 0;
		m_nPortamentoGlissando[i] = 0;
		m_nPortamentoTime[i] = 0;
		m_bMonoMode[i]=0; 
		m_nNoteLimitLow[i] = 0;
		m_nNoteLimitHigh[i] = 127;
		m_nNoteShift[i] = 0;
		
		m_nModulationWheelRange[i]=99;
		m_nModulationWheelTarget[i]=7;
		m_nFootControlRange[i]=99;
		m_nFootControlTarget[i]=0;	
		m_nBreathControlRange[i]=99;	
		m_nBreathControlTarget[i]=0;	
		m_nAftertouchRange[i]=99;	
		m_nAftertouchTarget[i]=0;

#if defined(MIXING_CONSOLE_ENABLE)
		memset(this->m_nFXSendLevel[i], 0, MixerOutput::kFXCount * sizeof(unsigned));
#elif defined(PLATE_REVERB_ENABLE)
		m_nReverbSend[i] = 0;
#endif
		m_uchOPMask[i] = 0b111111;	// All operators on

		m_pTG[i] = new CDexedAdapter (CConfig::MaxNotes, pConfig->GetSampleRate ());
		assert (m_pTG[i]);

		m_pTG[i]->activate ();
	}

#if defined(MIXING_CONSOLE_ENABLE)
	size_t end = MixerOutput::kFXCount - 1;
	for(size_t ret = 0; ret < end; ++ret)
	{
		memset(this->m_nFXReturnLevel[ret], 0, MixerOutput::kFXCount * sizeof(unsigned));
	}
#endif

	for (unsigned i = 0; i < CConfig::MaxUSBMIDIDevices; i++)
	{
		m_pMIDIKeyboard[i] = new CMIDIKeyboard (this, pConfig, &m_UI, i);
		assert (m_pMIDIKeyboard[i]);
	}

	// select the sound device
	const char *pDeviceName = pConfig->GetSoundDevice ();
	if (strcmp (pDeviceName, "i2s") == 0)
	{
		LOGNOTE ("I2S mode");

		m_pSoundDevice = new CI2SSoundBaseDevice (pInterrupt, pConfig->GetSampleRate (),
							  pConfig->GetChunkSize (), false,
							  pI2CMaster, pConfig->GetDACI2CAddress ());
	}
	else if (strcmp (pDeviceName, "hdmi") == 0)
	{
		LOGNOTE ("HDMI mode");

		m_pSoundDevice = new CHDMISoundBaseDevice (pInterrupt, pConfig->GetSampleRate (),
							   pConfig->GetChunkSize ());

		// The channels are swapped by default in the HDMI sound driver.
		// TODO: Remove this line, when this has been fixed in the driver.
		m_bChannelsSwapped = !m_bChannelsSwapped;
	}
	else
	{
		LOGNOTE ("PWM mode");

		m_pSoundDevice = new CPWMSoundBaseDevice (pInterrupt, pConfig->GetSampleRate (),
							  pConfig->GetChunkSize ());
	}

#ifdef ARM_ALLOW_MULTI_CORE
	for (unsigned nCore = 0; nCore < CORES; nCore++)
	{
		m_CoreStatus[nCore] = CoreStatusInit;
	}
#endif

	setMasterVolume(1.0);


#if defined(MIXING_CONSOLE_ENABLE)
	this->mixing_console_ = new Mixer(static_cast<float32_t>(pConfig->GetSampleRate()), pConfig->GetChunkSize()/2);

	// Tube parameters
	this->SetParameter(ParameterFXTubeEnable, 1);
	this->SetParameter(ParameterFXTubeOverdrive, 10);

	// Chorus parameters
	this->SetParameter(ParameterFXChorusEnable, 1);
	this->SetParameter(ParameterFXChorusRate, 50);
	this->SetParameter(ParameterFXChorusDepth, 50);
	
	// Flanger parameters
	this->SetParameter(ParameterFXFlangerEnable, 1);
	this->SetParameter(ParameterFXFlangerRate, 3);
	this->SetParameter(ParameterFXFlangerDepth, 75);
	this->SetParameter(ParameterFXFlangerFeedback, 50);

	// Orbitone parameters
	this->SetParameter(ParameterFXOrbitoneEnable, 1);
	this->SetParameter(ParameterFXOrbitoneRate, 40);
	this->SetParameter(ParameterFXOrbitoneDepth, 50);

	// Phaser parameters
	this->SetParameter(ParameterFXPhaserEnable, 1);
	this->SetParameter(ParameterFXPhaserRate, 5);
	this->SetParameter(ParameterFXPhaserDepth, 99);
	this->SetParameter(ParameterFXPhaserFeedback, 50);
	this->SetParameter(ParameterFXPhaserNbStages, 12);

	// Delay parameters
	this->SetParameter(ParameterFXDelayEnable, 1);
	this->SetParameter(ParameterFXDelayLeftDelayTime, 15);
	this->SetParameter(ParameterFXDelayRightDelayTime, 22);
	this->SetParameter(ParameterFXDelayFeedback, 35);

	// AudioEffectPlateReverb parameters
	this->SetParameter(ParameterReverbEnable, 1);
	this->SetParameter(ParameterReverbSize, 70);
	this->SetParameter(ParameterReverbHighDamp, 50);
	this->SetParameter(ParameterReverbLowDamp, 50);
	this->SetParameter(ParameterReverbLowPass, 30);
	this->SetParameter(ParameterReverbDiffusion, 65);
	this->SetParameter(ParameterReverbLevel, 99);

	// Reverberator parameters
	this->SetParameter(ParameterFXReverberatorEnable, 1);
	this->SetParameter(ParameterFXReverberatorInputGain, 99);
	this->SetParameter(ParameterFXReverberatorTime, 80);
	this->SetParameter(ParameterFXReverberatorDiffusion, 80);
	this->SetParameter(ParameterFXReverberatorLP, 70);

#elif defined(PLATE_REVERB_ENABLE)

	// BEGIN setup tg_mixer
	tg_mixer = new AudioStereoMixer<CConfig::ToneGenerators>(pConfig->GetChunkSize()/2);
	// END setup tgmixer

	// BEGIN setup reverb
	reverb_send_mixer = new AudioStereoMixer<CConfig::ToneGenerators>(pConfig->GetChunkSize()/2);
	reverb = new AudioEffectPlateReverb(pConfig->GetSampleRate());
	SetParameter (ParameterReverbEnable, 1);
	SetParameter (ParameterReverbSize, 70);
	SetParameter (ParameterReverbHighDamp, 50);
	SetParameter (ParameterReverbLowDamp, 50);
	SetParameter (ParameterReverbLowPass, 30);
	SetParameter (ParameterReverbDiffusion, 65);
	SetParameter (ParameterReverbLevel, 99);
	// END setup reverb
#endif

	SetParameter (ParameterCompressorEnable, 1);
};

bool CMiniDexed::Initialize (void)
{
	assert (m_pConfig);
	assert (m_pSoundDevice);

	if (!m_UI.Initialize ())
	{
		return false;
	}

	m_SysExFileLoader.Load ();

	if (m_SerialMIDI.Initialize ())
	{
		LOGNOTE ("Serial MIDI interface enabled");

		m_bUseSerial = true;
	}

	for (unsigned i = 0; i < CConfig::ToneGenerators; i++)
	{
		assert (m_pTG[i]);

		SetVolume (100, i);
		ProgramChange (0, i);

		m_pTG[i]->setTranspose (24);

		m_pTG[i]->setPBController (2, 0);
		m_pTG[i]->setMWController (99, 1, 0); 

		m_pTG[i]->setFCController (99, 1, 0); 
		m_pTG[i]->setBCController (99, 1, 0);
		m_pTG[i]->setATController (99, 1, 0);
		
#if defined(MIXING_CONSOLE_ENABLE)
		// setup the mixer so that it remains identical to the initial version of the synth
		this->mixing_console_->setPan(i, this->m_nPan[i] / 127.0f);
		float32_t sendRev = this->m_nFXSendLevel[i][MixerOutput::FX_PlateReverb] / 99.0f;
		this->mixing_console_->setSendLevel(i, MixerOutput::FX_PlateReverb, sendRev);
		this->mixing_console_->setSendLevel(i, MixerOutput::MainOutput, 1.0f - sendRev);

#elif defined(PLATE_REVERB_ENABLE)

		tg_mixer->pan(i,mapfloat(m_nPan[i],0,127,0.0f,1.0f));
		tg_mixer->gain(i,1.0f);
		reverb_send_mixer->pan(i,mapfloat(m_nPan[i],0,127,0.0f,1.0f));
		reverb_send_mixer->gain(i,mapfloat(m_nReverbSend[i],0,99,0.0f,1.0f));
#endif
	}

	if (m_PerformanceConfig.Load ())
	{
		LoadPerformanceParameters(); 
	}
	else
	{
		SetMIDIChannel (CMIDIDevice::OmniMode, 0);
	}

	// load performances file list, and attempt to create the performance folder
	if (!m_PerformanceConfig.ListPerformances()) 
	{
		LOGERR ("Cannot create internal Performance folder, new performances can't be created");
	}
	
	// setup and start the sound device
	if (!m_pSoundDevice->AllocateQueueFrames (m_pConfig->GetChunkSize ()))
	{
		LOGERR ("Cannot allocate sound queue");

		return false;
	}

#ifndef ARM_ALLOW_MULTI_CORE
	m_pSoundDevice->SetWriteFormat (SoundFormatSigned16, 1);	// 16-bit Mono
#else
	m_pSoundDevice->SetWriteFormat (SoundFormatSigned16, 2);	// 16-bit Stereo
#endif

	m_nQueueSizeFrames = m_pSoundDevice->GetQueueSizeFrames ();

	m_pSoundDevice->Start ();

#ifdef ARM_ALLOW_MULTI_CORE
	// start secondary cores
	if (!CMultiCoreSupport::Initialize ())
	{
		return false;
	}
#endif
	
	return true;
}

void CMiniDexed::Process (bool bPlugAndPlayUpdated)
{
#ifndef ARM_ALLOW_MULTI_CORE
	ProcessSound ();
#endif

	for (unsigned i = 0; i < CConfig::MaxUSBMIDIDevices; i++)
	{
		assert (m_pMIDIKeyboard[i]);
		m_pMIDIKeyboard[i]->Process (bPlugAndPlayUpdated);
	}

	m_PCKeyboard.Process (bPlugAndPlayUpdated);

	if (m_bUseSerial)
	{
		m_SerialMIDI.Process ();
	}

	m_UI.Process ();

	if (m_bSavePerformance)
	{
		DoSavePerformance ();

		m_bSavePerformance = false;
	}

	if (m_bSavePerformanceNewFile)
	{
		DoSavePerformanceNewFile ();
		m_bSavePerformanceNewFile = false;
	}
	
	if (m_bSetNewPerformance && !m_bLoadPerformanceBusy)
	{
		DoSetNewPerformance ();
		if (m_nSetNewPerformanceID == GetActualPerformanceID())
		{
			m_bSetNewPerformance = false;
		}
		
	}
	
	if(m_bDeletePerformance)
	{
		DoDeletePerformance ();
		m_bDeletePerformance = false;
	}
		
	if (m_bProfileEnabled)
	{
		m_GetChunkTimer.Dump ();
	}
}

#ifdef ARM_ALLOW_MULTI_CORE

void CMiniDexed::Run (unsigned nCore)
{
	assert (1 <= nCore && nCore < CORES);

	if (nCore == 1)
	{
		m_CoreStatus[nCore] = CoreStatusIdle;			// core 1 ready

		// wait for cores 2 and 3 to be ready
		for (unsigned nCore = 2; nCore < CORES; nCore++)
		{
			while (m_CoreStatus[nCore] != CoreStatusIdle)
			{
				// just wait
			}
		}

		while (m_CoreStatus[nCore] != CoreStatusExit)
		{
			ProcessSound ();
		}
	}
	else								// core 2 and 3
	{
		while (1)
		{
			m_CoreStatus[nCore] = CoreStatusIdle;		// ready to be kicked
			while (m_CoreStatus[nCore] == CoreStatusIdle)
			{
				// just wait
			}

			// now kicked from core 1

			if (m_CoreStatus[nCore] == CoreStatusExit)
			{
				m_CoreStatus[nCore] = CoreStatusUnknown;

				break;
			}

			assert (m_CoreStatus[nCore] == CoreStatusBusy);

			// process the TGs, assigned to this core (2 or 3)

			assert (m_nFramesToProcess <= CConfig::MaxChunkSize);
			unsigned nTG = CConfig::TGsCore1 + (nCore-2)*CConfig::TGsCore23;
			for (unsigned i = 0; i < CConfig::TGsCore23; i++, nTG++)
			{
				assert (m_pTG[nTG]);
				m_pTG[nTG]->getSamples (m_OutputLevel[nTG],m_nFramesToProcess);
			}
		}
	}
}

#endif

CSysExFileLoader *CMiniDexed::GetSysExFileLoader (void)
{
	return &m_SysExFileLoader;
}

void CMiniDexed::BankSelectLSB (unsigned nBankLSB, unsigned nTG)
{
	nBankLSB=constrain((int)nBankLSB,0,127);

	assert (nTG < CConfig::ToneGenerators);
	m_nVoiceBankID[nTG] = nBankLSB;

	m_UI.ParameterChanged ();
}

void CMiniDexed::ProgramChange (unsigned nProgram, unsigned nTG)
{
	assert (m_pConfig);

	nProgram=constrain((int)nProgram,0,31);

	assert (nTG < CConfig::ToneGenerators);
	m_nProgram[nTG] = nProgram;

	uint8_t Buffer[156];
	m_SysExFileLoader.GetVoice (m_nVoiceBankID[nTG], nProgram, Buffer);

	assert (m_pTG[nTG]);
	m_pTG[nTG]->loadVoiceParameters (Buffer);

	if (m_pConfig->GetMIDIAutoVoiceDumpOnPC())
	{
		// Only do the voice dump back out over MIDI if we have a specific
		// MIDI channel configured for this TG
		if (m_nMIDIChannel[nTG] < CMIDIDevice::Channels)
		{
			m_SerialMIDI.SendSystemExclusiveVoice(nProgram,0,nTG);
		}
	}

	m_UI.ParameterChanged ();
}

void CMiniDexed::SetVolume (unsigned nVolume, unsigned nTG)
{
	nVolume=constrain((int)nVolume,0,127);

	assert (nTG < CConfig::ToneGenerators);
	m_nVolume[nTG] = nVolume;

	assert (m_pTG[nTG]);
	m_pTG[nTG]->setGain (nVolume / 127.0f);

	m_UI.ParameterChanged ();
}

void CMiniDexed::SetPan (unsigned nPan, unsigned nTG)
{
	nPan=constrain((int)nPan,0,127);

	assert (nTG < CConfig::ToneGenerators);
	m_nPan[nTG] = nPan;

#if defined(MIXING_CONSOLE_ENABLE)
	this->mixing_console_->setPan(nTG, mapfloat(nPan, 0, 127, 0.0f, 1.0f));

#elif defined(PLATE_REVERB_ENABLE)

	tg_mixer->pan(nTG,mapfloat(nPan,0,127,0.0f,1.0f));
	reverb_send_mixer->pan(nTG,mapfloat(nPan,0,127,0.0f,1.0f));
#endif

	m_UI.ParameterChanged ();
}

#if defined(MIXING_CONSOLE_ENABLE)

void CMiniDexed::setMixingConsoleSendLevel(unsigned nTG, MixerOutput fx, unsigned nFXSend)
{
	assert (nTG < CConfig::ToneGenerators);
	nFXSend = constrain((int)nFXSend,0,99);

	this->m_nFXSendLevel[nTG][fx] = nFXSend;
	this->mixing_console_->setSendLevel(nTG, fx, nFXSend / 99.0f);

	this->m_UI.ParameterChanged ();
}

void CMiniDexed::setMixingConsoleReturnLevel(MixerOutput ret, MixerOutput fx, unsigned nFXReturn)
{
	assert (ret < (MixerOutput::kFXCount - 1));
	assert (fx < MixerOutput::kFXCount);
	if(ret == fx)
	{
		nFXReturn = 0;
	}
	else
	{
		nFXReturn = constrain((int)nFXReturn,0,99);
	}

	this->m_nFXReturnLevel[ret][fx] = nFXReturn;
	this->mixing_console_->setReturnLevel(ret, fx, nFXReturn / 99.0f);

	this->m_UI.ParameterChanged ();
}

#elif defined(PLATE_REVERB_ENABLE)

void CMiniDexed::SetReverbSend (unsigned nReverbSend, unsigned nTG)
{
	nReverbSend=constrain((int)nReverbSend,0,99);

	assert (nTG < CConfig::ToneGenerators);
	m_nReverbSend[nTG] = nReverbSend;

	reverb_send_mixer->gain(nTG,mapfloat(nReverbSend,0,99,0.0f,1.0f));
	
	m_UI.ParameterChanged ();
}

#endif

void CMiniDexed::SetMasterTune (int nMasterTune, unsigned nTG)
{
	nMasterTune=constrain((int)nMasterTune,-99,99);

	assert (nTG < CConfig::ToneGenerators);
	m_nMasterTune[nTG] = nMasterTune;

	assert (m_pTG[nTG]);
	m_pTG[nTG]->setMasterTune ((int8_t) nMasterTune);

	m_UI.ParameterChanged ();
}

void CMiniDexed::SetCutoff (int nCutoff, unsigned nTG)
{
	nCutoff = constrain (nCutoff, 0, 99);

	assert (nTG < CConfig::ToneGenerators);
	m_nCutoff[nTG] = nCutoff;

	assert (m_pTG[nTG]);
	m_pTG[nTG]->setFilterCutoff (mapfloat (nCutoff, 0, 99, 0.0f, 1.0f));

	m_UI.ParameterChanged ();
}

void CMiniDexed::SetResonance (int nResonance, unsigned nTG)
{
	nResonance = constrain (nResonance, 0, 99);

	assert (nTG < CConfig::ToneGenerators);
	m_nResonance[nTG] = nResonance;

	assert (m_pTG[nTG]);
	m_pTG[nTG]->setFilterResonance (mapfloat (nResonance, 0, 99, 0.0f, 1.0f));

	m_UI.ParameterChanged ();
}



void CMiniDexed::SetMIDIChannel (uint8_t uchChannel, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (uchChannel < CMIDIDevice::ChannelUnknown);

	m_nMIDIChannel[nTG] = uchChannel;

	for (unsigned i = 0; i < CConfig::MaxUSBMIDIDevices; i++)
	{
		assert (m_pMIDIKeyboard[i]);
		m_pMIDIKeyboard[i]->SetChannel (uchChannel, nTG);
	}

	m_PCKeyboard.SetChannel (uchChannel, nTG);

	if (m_bUseSerial)
	{
		m_SerialMIDI.SetChannel (uchChannel, nTG);
	}

#ifdef ARM_ALLOW_MULTI_CORE
	unsigned nActiveTGs = 0;
	for (unsigned nTG = 0; nTG < CConfig::ToneGenerators; nTG++)
	{
		if (m_nMIDIChannel[nTG] != CMIDIDevice::Disabled)
		{
			nActiveTGs++;
		}
	}

	assert (nActiveTGs <= 8);
	static const unsigned Log2[] = {0, 0, 1, 2, 2, 3, 3, 3, 3};
	m_nActiveTGsLog2 = Log2[nActiveTGs];
#endif

	m_UI.ParameterChanged ();
}

void CMiniDexed::keyup (int16_t pitch, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	pitch = ApplyNoteLimits (pitch, nTG);
	if (pitch >= 0)
	{
		m_pTG[nTG]->keyup (pitch);
	}
}

void CMiniDexed::keydown (int16_t pitch, uint8_t velocity, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	pitch = ApplyNoteLimits (pitch, nTG);
	if (pitch >= 0)
	{
		m_pTG[nTG]->keydown (pitch, velocity);
	}
}

int16_t CMiniDexed::ApplyNoteLimits (int16_t pitch, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);

	if (   pitch < (int16_t) m_nNoteLimitLow[nTG]
	    || pitch > (int16_t) m_nNoteLimitHigh[nTG])
	{
		return -1;
	}

	pitch += m_nNoteShift[nTG];

	if (   pitch < 0
	    || pitch > 127)
	{
		return -1;
	}

	return pitch;
}

void CMiniDexed::setSustain(bool sustain, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_pTG[nTG]->setSustain (sustain);
}

void CMiniDexed::panic(uint8_t value, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	if (value == 0) {
		m_pTG[nTG]->panic ();
	}
}

void CMiniDexed::notesOff(uint8_t value, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	if (value == 0) {
		m_pTG[nTG]->notesOff ();
	}
}

void CMiniDexed::setModWheel (uint8_t value, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_pTG[nTG]->setModWheel (value);
}


void CMiniDexed::setFootController (uint8_t value, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_pTG[nTG]->setFootController (value);
}

void CMiniDexed::setBreathController (uint8_t value, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_pTG[nTG]->setBreathController (value);
}

void CMiniDexed::setAftertouch (uint8_t value, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_pTG[nTG]->setAftertouch (value);
}

void CMiniDexed::setPitchbend (int16_t value, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_pTG[nTG]->setPitchbend (value);
}

void CMiniDexed::ControllersRefresh (unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_pTG[nTG]->ControllersRefresh ();
}

void CMiniDexed::SetParameter (TParameter Parameter, int nValue)
{
#if defined(MIXING_CONSOLE_ENABLE)
	assert(this->mixing_console_);
#elif defined(PLATE_REVERB_ENABLE)
	assert(reverb);
#endif

	assert(Parameter < ParameterUnknown);
	m_nParameter[Parameter] = nValue;

	switch (Parameter)
	{
	case ParameterCompressorEnable:
		for (unsigned nTG = 0; nTG < CConfig::ToneGenerators; nTG++)
		{
			assert(m_pTG[nTG]);
			m_pTG[nTG]->setCompressor (!!nValue);
		}
		break;

#if defined(MIXING_CONSOLE_ENABLE)
	// Tube parameters
	case ParameterFXTubeEnable: 
		nValue = constrain((int)nValue, 0, 1);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getTube()->setMute(!nValue);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXTubeOverdrive: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getTube()->setOverdrive(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	
	// Chorus parameters
	case ParameterFXChorusEnable: 
		nValue = constrain((int)nValue, 0, 1);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getChorus()->setMute(!nValue);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXChorusRate: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getChorus()->setRate(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXChorusDepth: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getChorus()->setDepth(nValue / 9.9f);
		this->m_FXSpinLock.Release();
		break;
	
	// Flanger parameters
	case ParameterFXFlangerEnable: 
		nValue = constrain((int)nValue, 0, 1);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getFlanger()->setMute(!nValue);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXFlangerRate: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getFlanger()->setRate(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXFlangerDepth: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getFlanger()->setDepth(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXFlangerFeedback: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getFlanger()->setFeedback(mapfloat(nValue, 0, 99, 0.0f, 0.97f));
		this->m_FXSpinLock.Release();
		break;
	
	// Orbitone parameters
	case ParameterFXOrbitoneEnable: 
		nValue = constrain((int)nValue, 0, 1);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getOrbitone()->setMute(!nValue);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXOrbitoneRate: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getOrbitone()->setRate(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXOrbitoneDepth: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getOrbitone()->setDepth(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	
	// Phaser parameters
	case ParameterFXPhaserEnable: 
		nValue = constrain((int)nValue, 0, 1);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getPhaser()->setMute(!nValue);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXPhaserRate: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getPhaser()->setRate(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXPhaserDepth: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getPhaser()->setDepth(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXPhaserFeedback: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getPhaser()->setFeedback(mapfloat(nValue, 0, 99, 0.0f, 0.97f));
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXPhaserNbStages: 
		nValue = constrain((int)nValue, 2, MAX_NB_PHASES);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getPhaser()->setNbStages(nValue);
		this->m_FXSpinLock.Release();
		break;

	// Delay parameters
	case ParameterFXDelayEnable: 
		nValue = constrain((int)nValue, 0, 1);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getDelay()->setMute(!nValue);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXDelayLeftDelayTime: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getDelay()->setLeftDelayTime(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXDelayRightDelayTime: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getDelay()->setRightDelayTime(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXDelayFeedback: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getDelay()->setFeedback(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;

	// AudioEffectPlateReverb parameters
	case ParameterReverbEnable:
		nValue=constrain((int)nValue,0,1);
		this->m_FXSpinLock.Acquire ();
		this->mixing_console_->getPlateReverb()->set_bypass (!nValue);
		this->m_FXSpinLock.Release ();
		break;
	case ParameterReverbSize:
		nValue=constrain((int)nValue,0,99);
		this->m_FXSpinLock.Acquire ();
		this->mixing_console_->getPlateReverb()->size (nValue / 99.0f);
		this->m_FXSpinLock.Release ();
		break;
	case ParameterReverbHighDamp:
		nValue=constrain((int)nValue,0,99);
		this->m_FXSpinLock.Acquire ();
		this->mixing_console_->getPlateReverb()->hidamp (nValue / 99.0f);
		this->m_FXSpinLock.Release ();
		break;
	case ParameterReverbLowDamp:
		nValue=constrain((int)nValue,0,99);
		this->m_FXSpinLock.Acquire ();
		this->mixing_console_->getPlateReverb()->lodamp (nValue / 99.0f);
		this->m_FXSpinLock.Release ();
		break;
	case ParameterReverbLowPass:
		nValue=constrain((int)nValue,0,99);
		this->m_FXSpinLock.Acquire ();
		this->mixing_console_->getPlateReverb()->lowpass (nValue / 99.0f);
		this->m_FXSpinLock.Release ();
		break;
	case ParameterReverbDiffusion:
		nValue=constrain((int)nValue,0,99);
		this->m_FXSpinLock.Acquire ();
		this->mixing_console_->getPlateReverb()->diffusion (nValue / 99.0f);
		this->m_FXSpinLock.Release ();
		break;
	case ParameterReverbLevel:
		nValue=constrain((int)nValue,0,99);
		this->m_FXSpinLock.Acquire ();
		this->mixing_console_->getPlateReverb()->level (nValue / 99.0f);
		this->m_FXSpinLock.Release ();
		break;

	// Reverberator parameters
	case ParameterFXReverberatorEnable: 
		nValue = constrain((int)nValue, 0, 1);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getReverberator()->setMute(!nValue);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXReverberatorInputGain: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getReverberator()->setInputGain(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXReverberatorTime: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getReverberator()->setTime(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXReverberatorDiffusion: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getReverberator()->setDiffusion(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;
	case ParameterFXReverberatorLP: 
		nValue = constrain((int)nValue, 0, 99);
		this->m_FXSpinLock.Acquire();
		this->mixing_console_->getReverberator()->setLP(nValue / 99.0f);
		this->m_FXSpinLock.Release();
		break;

#elif defined(PLATE_REVERB_ENABLE)

	case ParameterReverbEnable:
		nValue=constrain((int)nValue,0,1);
		m_FXSpinLock.Acquire ();
		reverb->set_bypass (!nValue);
		m_FXSpinLock.Release ();
		break;

	case ParameterReverbSize:
		nValue=constrain((int)nValue,0,99);
		m_FXSpinLock.Acquire ();
		reverb->size (nValue / 99.0f);
		m_FXSpinLock.Release ();
		break;

	case ParameterReverbHighDamp:
		nValue=constrain((int)nValue,0,99);
		m_FXSpinLock.Acquire ();
		reverb->hidamp (nValue / 99.0f);
		m_FXSpinLock.Release ();
		break;

	case ParameterReverbLowDamp:
		nValue=constrain((int)nValue,0,99);
		m_FXSpinLock.Acquire ();
		reverb->lodamp (nValue / 99.0f);
		m_FXSpinLock.Release ();
		break;

	case ParameterReverbLowPass:
		nValue=constrain((int)nValue,0,99);
		m_FXSpinLock.Acquire ();
		reverb->lowpass (nValue / 99.0f);
		m_FXSpinLock.Release ();
		break;

	case ParameterReverbDiffusion:
		nValue=constrain((int)nValue,0,99);
		m_FXSpinLock.Acquire ();
		reverb->diffusion (nValue / 99.0f);
		m_FXSpinLock.Release ();
		break;

	case ParameterReverbLevel:
		nValue=constrain((int)nValue,0,99);
		m_FXSpinLock.Acquire ();
		reverb->level (nValue / 99.0f);
		m_FXSpinLock.Release ();
		break;
#endif

	default:
		assert (0);
		break;
	}
}

int CMiniDexed::GetParameter (TParameter Parameter)
{
	assert (Parameter < ParameterUnknown);
	return m_nParameter[Parameter];
}

void CMiniDexed::SetTGParameter (TTGParameter Parameter, int nValue, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);

	switch (Parameter)
	{
	case TGParameterVoiceBank:				BankSelectLSB (nValue, nTG);	break;
	case TGParameterProgram:				ProgramChange (nValue, nTG);	break;
	case TGParameterVolume:					SetVolume (nValue, nTG);	break;
	case TGParameterPan:					SetPan (nValue, nTG);		break;
	case TGParameterMasterTune:				SetMasterTune (nValue, nTG);	break;
	case TGParameterCutoff:					SetCutoff (nValue, nTG);	break;
	case TGParameterResonance:				SetResonance (nValue, nTG);	break;
	case TGParameterPitchBendRange:			setPitchbendRange (nValue, nTG);	break;
	case TGParameterPitchBendStep:			setPitchbendStep (nValue, nTG);	break;
	case TGParameterPortamentoMode:			setPortamentoMode (nValue, nTG);	break;
	case TGParameterPortamentoGlissando:	setPortamentoGlissando (nValue, nTG);	break;
	case TGParameterPortamentoTime:			setPortamentoTime (nValue, nTG);	break;
	case TGParameterMonoMode:				setMonoMode (nValue , nTG);	break; 
	
	case TGParameterMWRange:				setModController(0, 0, nValue, nTG); break;
	case TGParameterMWPitch:				setModController(0, 1, nValue, nTG); break;
	case TGParameterMWAmplitude:			setModController(0, 2, nValue, nTG); break;
	case TGParameterMWEGBias:				setModController(0, 3, nValue, nTG); break;
	
	case TGParameterFCRange:				setModController(1, 0, nValue, nTG); break;
	case TGParameterFCPitch:				setModController(1, 1, nValue, nTG); break;
	case TGParameterFCAmplitude:			setModController(1, 2, nValue, nTG); break;
	case TGParameterFCEGBias:				setModController(1, 3, nValue, nTG); break;
	
	case TGParameterBCRange:				setModController(2, 0, nValue, nTG); break;
	case TGParameterBCPitch:				setModController(2, 1, nValue, nTG); break;
	case TGParameterBCAmplitude:			setModController(2, 2, nValue, nTG); break;
	case TGParameterBCEGBias:				setModController(2, 3, nValue, nTG); break;
	
	case TGParameterATRange:				setModController(3, 0, nValue, nTG); break;
	case TGParameterATPitch:				setModController(3, 1, nValue, nTG); break;
	case TGParameterATAmplitude:			setModController(3, 2, nValue, nTG); break;
	case TGParameterATEGBias:				setModController(3, 3, nValue, nTG); break;
	
	case TGParameterMIDIChannel:
		assert (0 <= nValue && nValue <= 255);
		SetMIDIChannel ((uint8_t) nValue, nTG);
		break;

#if defined(MIXING_CONSOLE_ENABLE)
	case TGParameterMixingSendFXTube:			this->setMixingConsoleSendLevel(nTG, MixerOutput::FX_Tube, 			nValue); break;
	case TGParameterMixingSendFXChorus:			this->setMixingConsoleSendLevel(nTG, MixerOutput::FX_Chorus, 		nValue); break;
	case TGParameterMixingSendFXFlanger:		this->setMixingConsoleSendLevel(nTG, MixerOutput::FX_Flanger, 		nValue); break;
	case TGParameterMixingSendFXOrbittone:		this->setMixingConsoleSendLevel(nTG, MixerOutput::FX_Orbitone, 		nValue); break;
	case TGParameterMixingSendFXPhaser:			this->setMixingConsoleSendLevel(nTG, MixerOutput::FX_Phaser, 		nValue); break;
	case TGParameterMixingSendFXDelay:			this->setMixingConsoleSendLevel(nTG, MixerOutput::FX_Delay, 		nValue); break;
	case TGParameterMixingSendFXPlateReverb:	this->setMixingConsoleSendLevel(nTG, MixerOutput::FX_PlateReverb, 	nValue); break;
	case TGParameterMixingSendFXReverberator:	this->setMixingConsoleSendLevel(nTG, MixerOutput::FX_Reverberator, 	nValue); break;
	case TGParameterMixingSendFXMainOutput:		this->setMixingConsoleSendLevel(nTG, MixerOutput::MainOutput, 		nValue); break;
#elif defined(PLATE_REVERB_ENABLE)
	case TGParameterReverbSend:	SetReverbSend (nValue, nTG);	break;
#endif // MIXING_CONSOLE_ENABLE

	default:
		assert (0);
		break;
	}
}

int CMiniDexed::GetTGParameter (TTGParameter Parameter, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);

	switch (Parameter)
	{
	case TGParameterVoiceBank:				return m_nVoiceBankID[nTG];
	case TGParameterProgram:				return m_nProgram[nTG];
	case TGParameterVolume:					return m_nVolume[nTG];
	case TGParameterPan:					return m_nPan[nTG];
	case TGParameterMasterTune:				return m_nMasterTune[nTG];
	case TGParameterCutoff:					return m_nCutoff[nTG];
	case TGParameterResonance:				return m_nResonance[nTG];
	case TGParameterMIDIChannel:			return m_nMIDIChannel[nTG];
#if defined(PLATE_REVERB_ENABLE)
	case TGParameterReverbSend:	return m_nReverbSend[nTG];
#endif
	case TGParameterPitchBendRange:			return m_nPitchBendRange[nTG];
	case TGParameterPitchBendStep:			return m_nPitchBendStep[nTG];
	case TGParameterPortamentoMode:			return m_nPortamentoMode[nTG];
	case TGParameterPortamentoGlissando:	return m_nPortamentoGlissando[nTG];
	case TGParameterPortamentoTime:			return m_nPortamentoTime[nTG];
	case TGParameterMonoMode:				return m_bMonoMode[nTG] ? 1 : 0; 
	
	case TGParameterMWRange:				return getModController(0, 0, nTG);
	case TGParameterMWPitch:				return getModController(0, 1, nTG);
	case TGParameterMWAmplitude:			return getModController(0, 2, nTG); 
	case TGParameterMWEGBias:				return getModController(0, 3, nTG); 
	
	case TGParameterFCRange:				return getModController(1, 0,  nTG); 
	case TGParameterFCPitch:				return getModController(1, 1,  nTG); 
	case TGParameterFCAmplitude:			return getModController(1, 2,  nTG); 
	case TGParameterFCEGBias:				return getModController(1, 3,  nTG); 
	
	case TGParameterBCRange:				return getModController(2, 0,  nTG); 
	case TGParameterBCPitch:				return getModController(2, 1,  nTG); 
	case TGParameterBCAmplitude:			return getModController(2, 2,  nTG); 
	case TGParameterBCEGBias:				return getModController(2, 3,  nTG); 
	
	case TGParameterATRange:				return getModController(3, 0,  nTG); 
	case TGParameterATPitch:				return getModController(3, 1,  nTG); 
	case TGParameterATAmplitude:			return getModController(3, 2,  nTG); 
	case TGParameterATEGBias:				return getModController(3, 3,  nTG); 
		
	default:
		assert (0);
		return 0;
	}
}

void CMiniDexed::SetVoiceParameter (uint8_t uchOffset, uint8_t uchValue, unsigned nOP, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	assert (nOP <= 6);

	if (nOP < 6)
	{
		if (uchOffset == DEXED_OP_ENABLE)
		{
			if (uchValue)
			{
				m_uchOPMask[nTG] |= 1 << nOP;
			}
			else
			{
				m_uchOPMask[nTG] &= ~(1 << nOP);
			}

			m_pTG[nTG]->setOPAll (m_uchOPMask[nTG]);

			return;
		}

		nOP = 5 - nOP;		// OPs are in reverse order
	}

	uchOffset += nOP * 21;
	assert (uchOffset < 156);

	m_pTG[nTG]->setVoiceDataElement (uchOffset, uchValue);
}

uint8_t CMiniDexed::GetVoiceParameter (uint8_t uchOffset, unsigned nOP, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	assert (nOP <= 6);

	if (nOP < 6)
	{
		if (uchOffset == DEXED_OP_ENABLE)
		{
			return !!(m_uchOPMask[nTG] & (1 << nOP));
		}

		nOP = 5 - nOP;		// OPs are in reverse order
	}

	uchOffset += nOP * 21;
	assert (uchOffset < 156);

	return m_pTG[nTG]->getVoiceDataElement (uchOffset);
}

std::string CMiniDexed::GetVoiceName (unsigned nTG)
{
	char VoiceName[11];
	memset (VoiceName, 0, sizeof VoiceName);

	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_pTG[nTG]->setName (VoiceName);

	std::string Result (VoiceName);

	return Result;
}

#ifndef ARM_ALLOW_MULTI_CORE

void CMiniDexed::ProcessSound (void)
{
	assert (m_pSoundDevice);

	unsigned nFrames = m_nQueueSizeFrames - m_pSoundDevice->GetQueueFramesAvail ();
	if (nFrames >= m_nQueueSizeFrames/2)
	{
		if (m_bProfileEnabled)
		{
			m_GetChunkTimer.Start ();
		}

		float32_t SampleBuffer[nFrames];
		m_pTG[0]->getSamples (SampleBuffer, nFrames);

		// Convert single float array (mono) to int16 array
		int16_t tmp_int[nFrames];
		arm_float_to_q15(SampleBuffer,tmp_int,nFrames);

		if (m_pSoundDevice->Write (tmp_int, sizeof(tmp_int)) != (int) sizeof(tmp_int))
		{
			LOGERR ("Sound data dropped");
		}

		if (m_bProfileEnabled)
		{
			m_GetChunkTimer.Stop ();
		}
	}
}

#else	// #ifdef ARM_ALLOW_MULTI_CORE

void CMiniDexed::ProcessSound (void)
{
	assert (m_pSoundDevice);

	unsigned nFrames = m_nQueueSizeFrames - m_pSoundDevice->GetQueueFramesAvail ();
	if (nFrames >= m_nQueueSizeFrames/2)
	{
		if (m_bProfileEnabled)
		{
			m_GetChunkTimer.Start ();
		}

		m_nFramesToProcess = nFrames;

		// kick secondary cores
		for (unsigned nCore = 2; nCore < CORES; nCore++)
		{
			assert (m_CoreStatus[nCore] == CoreStatusIdle);
			m_CoreStatus[nCore] = CoreStatusBusy;
		}

		// process the TGs assigned to core 1
		assert (nFrames <= CConfig::MaxChunkSize);
		for (unsigned i = 0; i < CConfig::TGsCore1; i++)
		{
			assert (m_pTG[i]);
			m_pTG[i]->getSamples (m_OutputLevel[i], nFrames);
		}

		// wait for cores 2 and 3 to complete their work
		for (unsigned nCore = 2; nCore < CORES; nCore++)
		{
			while (m_CoreStatus[nCore] != CoreStatusIdle)
			{
				// just wait
			}
		}

		//
		// Audio signal path after tone generators starts here
		//

#if defined(MIXING_CONSOLE_ENABLE)
		// // swap stereo channels if needed
		uint8_t indexL = StereoChannels::Left;
		uint8_t indexR = StereoChannels::Right;
		if(this->m_bChannelsSwapped)
		{
			indexL = StereoChannels::Right;
			indexR = StereoChannels::Left;
		}
		
		// BEGIN TG mixing
		float32_t tmp_float[nFrames * 2];
		int16_t tmp_int[nFrames * 2];

		float32_t SampleBuffer[StereoChannels::kNumChannels][nFrames];

		if(this->nMasterVolume > 0.0f)
		{
			this->m_FXSpinLock.Acquire ();

			for (uint8_t i = 0; i < CConfig::ToneGenerators; i++)
			{
				this->mixing_console_->setInputSampleBuffer(i, m_OutputLevel[i]);
			}

			this->mixing_console_->process(SampleBuffer[indexL], SampleBuffer[indexR]);
			this->m_FXSpinLock.Release ();

			// Convert dual float array (left, right) to single int16 array (left/right)
			this->nMasterVolume = constrain(this->nMasterVolume, 0.0f, 1.0f);
			if(this->nMasterVolume == 1.0f)
			{
				memcpy(tmp_float, 			SampleBuffer[indexL], nFrames * sizeof(float32_t));
				memcpy(tmp_float + nFrames, SampleBuffer[indexR], nFrames * sizeof(float32_t));
			}
			else // 0.0 < this->nMasterVolume < 1.0
			{
				arm_scale_f32(SampleBuffer[indexL], this->nMasterVolume, tmp_float, 			nFrames);
				arm_scale_f32(SampleBuffer[indexR], this->nMasterVolume, tmp_float + nFrames, 	nFrames);
			}
			arm_float_to_q15(tmp_float, tmp_int, nFrames * 2);
		}
		else
			arm_fill_q15(0, tmp_int, nFrames * 2);

#elif defined(PLATE_REVERB_ENABLE)

		uint8_t indexL=0, indexR=1;
		
		// BEGIN TG mixing
		float32_t tmp_float[nFrames*2];
		int16_t tmp_int[nFrames*2];

		if(nMasterVolume > 0.0f)
		{
			for (uint8_t i = 0; i < CConfig::ToneGenerators; i++)
			{
				tg_mixer->doAddMix(i,m_OutputLevel[i]);
				reverb_send_mixer->doAddMix(i,m_OutputLevel[i]);
			}
			// END TG mixing
	
			// BEGIN create SampleBuffer for holding audio data
			float32_t SampleBuffer[2][nFrames];
			// END create SampleBuffer for holding audio data

			// get the mix of all TGs
			tg_mixer->getMix(SampleBuffer[indexL], SampleBuffer[indexR]);

			// BEGIN adding reverb
			if (m_nParameter[ParameterReverbEnable])
			{
				float32_t ReverbBuffer[2][nFrames];
				float32_t ReverbSendBuffer[2][nFrames];

				arm_fill_f32(0.0f, ReverbBuffer[indexL], nFrames);
				arm_fill_f32(0.0f, ReverbBuffer[indexR], nFrames);
				arm_fill_f32(0.0f, ReverbSendBuffer[indexR], nFrames);
				arm_fill_f32(0.0f, ReverbSendBuffer[indexL], nFrames);
	
				m_FXSpinLock.Acquire ();
	
				reverb_send_mixer->getMix(ReverbSendBuffer[indexL], ReverbSendBuffer[indexR]);
				reverb->doReverb(ReverbSendBuffer[indexL],ReverbSendBuffer[indexR],ReverbBuffer[indexL], ReverbBuffer[indexR],nFrames);
	
				// scale down and add left reverb buffer by reverb level 
				arm_scale_f32(ReverbBuffer[indexL], reverb->get_level(), ReverbBuffer[indexL], nFrames);
				arm_add_f32(SampleBuffer[indexL], ReverbBuffer[indexL], SampleBuffer[indexL], nFrames);
				// scale down and add right reverb buffer by reverb level 
				arm_scale_f32(ReverbBuffer[indexR], reverb->get_level(), ReverbBuffer[indexR], nFrames);
				arm_add_f32(SampleBuffer[indexR], ReverbBuffer[indexR], SampleBuffer[indexR], nFrames);
	
				m_FXSpinLock.Release ();
			}
			// END adding reverb
	
			// swap stereo channels if needed prior to writing back out
			if (m_bChannelsSwapped)
			{
				indexL=1;
				indexR=0;
			}

			// Convert dual float array (left, right) to single int16 array (left/right)
			for(uint16_t i=0; i<nFrames;i++)
			{
				if(nMasterVolume >0.0 && nMasterVolume <1.0)
				{
					tmp_float[i*2]=SampleBuffer[indexL][i] * nMasterVolume;
					tmp_float[(i*2)+1]=SampleBuffer[indexR][i] * nMasterVolume;
				}
				else if(nMasterVolume == 1.0)
				{
					tmp_float[i*2]=SampleBuffer[indexL][i];
					tmp_float[(i*2)+1]=SampleBuffer[indexR][i];
				}
			}
			arm_float_to_q15(tmp_float,tmp_int,nFrames*2);
		}
		else
			arm_fill_q15(0, tmp_int, nFrames * 2);
#endif

		if(this->m_pSoundDevice->Write(tmp_int, sizeof(tmp_int)) != (int)sizeof(tmp_int))
		{
			LOGERR ("Sound data dropped");
		}

		if(this->m_bProfileEnabled)
		{
			this->m_GetChunkTimer.Stop ();
		}
	}
}

#endif

bool CMiniDexed::SavePerformance (bool bSaveAsDeault)
{
	m_bSavePerformance = true;
	m_bSaveAsDeault=bSaveAsDeault;

	return true;
}

bool CMiniDexed::DoSavePerformance (void)
{
	for (unsigned nTG = 0; nTG < CConfig::ToneGenerators; nTG++)
	{
		m_PerformanceConfig.SetBankNumber (m_nVoiceBankID[nTG], nTG);
		m_PerformanceConfig.SetVoiceNumber (m_nProgram[nTG], nTG);
		m_PerformanceConfig.SetMIDIChannel (m_nMIDIChannel[nTG], nTG);
		m_PerformanceConfig.SetVolume (m_nVolume[nTG], nTG);
		m_PerformanceConfig.SetPan (m_nPan[nTG], nTG);
		m_PerformanceConfig.SetDetune (m_nMasterTune[nTG], nTG);
		m_PerformanceConfig.SetCutoff (m_nCutoff[nTG], nTG);
		m_PerformanceConfig.SetResonance (m_nResonance[nTG], nTG);
		m_PerformanceConfig.SetPitchBendRange (m_nPitchBendRange[nTG], nTG);
		m_PerformanceConfig.SetPitchBendStep	(m_nPitchBendStep[nTG], nTG);
		m_PerformanceConfig.SetPortamentoMode (m_nPortamentoMode[nTG], nTG);
		m_PerformanceConfig.SetPortamentoGlissando (m_nPortamentoGlissando[nTG], nTG);
		m_PerformanceConfig.SetPortamentoTime (m_nPortamentoTime[nTG], nTG);

		m_PerformanceConfig.SetNoteLimitLow (m_nNoteLimitLow[nTG], nTG);
		m_PerformanceConfig.SetNoteLimitHigh (m_nNoteLimitHigh[nTG], nTG);
		m_PerformanceConfig.SetNoteShift (m_nNoteShift[nTG], nTG);
		m_pTG[nTG]->getVoiceData(m_nRawVoiceData);  
 		m_PerformanceConfig.SetVoiceDataToTxt (m_nRawVoiceData, nTG); 
		m_PerformanceConfig.SetMonoMode (m_bMonoMode[nTG], nTG); 
				
		m_PerformanceConfig.SetModulationWheelRange (m_nModulationWheelRange[nTG], nTG);
		m_PerformanceConfig.SetModulationWheelTarget (m_nModulationWheelTarget[nTG], nTG);
		m_PerformanceConfig.SetFootControlRange (m_nFootControlRange[nTG], nTG);
		m_PerformanceConfig.SetFootControlTarget (m_nFootControlTarget[nTG], nTG);
		m_PerformanceConfig.SetBreathControlRange (m_nBreathControlRange[nTG], nTG);
		m_PerformanceConfig.SetBreathControlTarget (m_nBreathControlTarget[nTG], nTG);
		m_PerformanceConfig.SetAftertouchRange (m_nAftertouchRange[nTG], nTG);
		m_PerformanceConfig.SetAftertouchTarget (m_nAftertouchTarget[nTG], nTG);

#ifdef MIXING_CONSOLE_ENABLE
		for(size_t fx = 0; fx < MixerOutput::kFXCount; ++fx)
		{
			this->m_PerformanceConfig.SetFXSendLevel(nTG, static_cast<MixerOutput>(fx), this->m_nFXSendLevel[nTG][fx]);
		}
#endif

#ifdef PLATE_REVERB_ENABLE
		m_PerformanceConfig.SetReverbSend (m_nReverbSend[nTG], nTG);
#endif
	}

	m_PerformanceConfig.SetCompressorEnable (!!m_nParameter[ParameterCompressorEnable]);
	m_PerformanceConfig.SetReverbEnable (!!m_nParameter[ParameterReverbEnable]);
	m_PerformanceConfig.SetReverbSize (m_nParameter[ParameterReverbSize]);
	m_PerformanceConfig.SetReverbHighDamp (m_nParameter[ParameterReverbHighDamp]);
	m_PerformanceConfig.SetReverbLowDamp (m_nParameter[ParameterReverbLowDamp]);
	m_PerformanceConfig.SetReverbLowPass (m_nParameter[ParameterReverbLowPass]);
	m_PerformanceConfig.SetReverbDiffusion (m_nParameter[ParameterReverbDiffusion]);
	m_PerformanceConfig.SetReverbLevel (m_nParameter[ParameterReverbLevel]);

#ifdef MIXING_CONSOLE_ENABLE
	this->m_PerformanceConfig.SetFXTubeEnable(!!this->m_nParameter[ParameterFXTubeEnable]);
	this->m_PerformanceConfig.SetFXTubeOverdrive(this->m_nParameter[ParameterFXTubeOverdrive]);
	this->m_PerformanceConfig.SetFXChorusEnable(!!this->m_nParameter[ParameterFXChorusEnable]);
	this->m_PerformanceConfig.SetFXChorusRate(this->m_nParameter[ParameterFXChorusRate]);
	this->m_PerformanceConfig.SetFXChorusDepth(this->m_nParameter[ParameterFXChorusDepth]);
	this->m_PerformanceConfig.SetFXFlangerEnable(!!this->m_nParameter[ParameterFXFlangerEnable]);
	this->m_PerformanceConfig.SetFXFlangerRate(this->m_nParameter[ParameterFXFlangerRate]);
	this->m_PerformanceConfig.SetFXFlangerDepth(this->m_nParameter[ParameterFXFlangerDepth]);
	this->m_PerformanceConfig.SetFXFlangerFeedback(this->m_nParameter[ParameterFXFlangerFeedback]);
	this->m_PerformanceConfig.SetFXOrbitoneEnable(!!this->m_nParameter[ParameterFXOrbitoneEnable]);
	this->m_PerformanceConfig.SetFXOrbitoneRate(this->m_nParameter[ParameterFXOrbitoneRate]);
	this->m_PerformanceConfig.SetFXOrbitoneDepth(this->m_nParameter[ParameterFXOrbitoneDepth]);
	this->m_PerformanceConfig.SetFXPhaserEnable(!!this->m_nParameter[ParameterFXPhaserEnable]);
	this->m_PerformanceConfig.SetFXPhaserRate(this->m_nParameter[ParameterFXPhaserRate]);
	this->m_PerformanceConfig.SetFXPhaserDepth(this->m_nParameter[ParameterFXPhaserDepth]);
	this->m_PerformanceConfig.SetFXPhaserFeedback(this->m_nParameter[ParameterFXPhaserFeedback]);
	this->m_PerformanceConfig.SetFXPhaserNbStages(this->m_nParameter[ParameterFXPhaserNbStages]);
	this->m_PerformanceConfig.SetFXDelayEnable(!!this->m_nParameter[ParameterFXDelayEnable]);
	this->m_PerformanceConfig.SetFXDelayLeftDelayTime(this->m_nParameter[ParameterFXDelayLeftDelayTime]);
	this->m_PerformanceConfig.SetFXDelayRightDelayTime(this->m_nParameter[ParameterFXDelayRightDelayTime]);
	this->m_PerformanceConfig.SetFXDelayFeedback(this->m_nParameter[ParameterFXDelayFeedback]);
	this->m_PerformanceConfig.SetFXReverberatorEnable(!!this->m_nParameter[ParameterFXReverberatorEnable]);
	this->m_PerformanceConfig.SetFXReverberatorInputGain(this->m_nParameter[ParameterFXReverberatorInputGain]);
	this->m_PerformanceConfig.SetFXReverberatorTime(this->m_nParameter[ParameterFXReverberatorTime]);
	this->m_PerformanceConfig.SetFXReverberatorDiffusion(this->m_nParameter[ParameterFXReverberatorDiffusion]);
	this->m_PerformanceConfig.SetFXReverberatorLP(this->m_nParameter[ParameterFXReverberatorLP]);

	size_t end = MixerOutput::kFXCount - 1;
	for(size_t ret = 0; ret < end; ++ret)
	{
		for(size_t fx = 0; fx < MixerOutput::kFXCount; ++fx)
		{
			this->m_PerformanceConfig.SetFXReturnLevel(static_cast<MixerOutput>(ret), static_cast<MixerOutput>(fx), this->m_nFXReturnLevel[ret][fx]);
		}
	}
#endif

	if(m_bSaveAsDeault)
	{
		m_PerformanceConfig.SetNewPerformance(0);
		
	}
	return m_PerformanceConfig.Save ();
}

void CMiniDexed::setMonoMode(uint8_t mono, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_bMonoMode[nTG]= mono != 0; 
	m_pTG[nTG]->setMonoMode(constrain(mono, 0, 1));
	m_pTG[nTG]->doRefreshVoice();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setPitchbendRange(uint8_t range, uint8_t nTG)
{
	range = constrain (range, 0, 12);
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_nPitchBendRange[nTG] = range;
	
	m_pTG[nTG]->setPitchbendRange(range);
	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setPitchbendStep(uint8_t step, uint8_t nTG)
{
	step= constrain (step, 0, 12);
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_nPitchBendStep[nTG] = step;
	
	m_pTG[nTG]->setPitchbendStep(step);
	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setPortamentoMode(uint8_t mode, uint8_t nTG)
{
	mode= constrain (mode, 0, 1);

	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_nPortamentoMode[nTG] = mode;
	
	m_pTG[nTG]->setPortamentoMode(mode);
	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setPortamentoGlissando(uint8_t glissando, uint8_t nTG)
{
	glissando = constrain (glissando, 0, 1);
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_nPortamentoGlissando[nTG] = glissando;
	
	m_pTG[nTG]->setPortamentoGlissando(glissando);
	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setPortamentoTime(uint8_t time, uint8_t nTG)
{
	time = constrain (time, 0, 99);
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	m_nPortamentoTime[nTG] = time;
	
	m_pTG[nTG]->setPortamentoTime(time);
	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setModWheelRange(uint8_t range, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	m_nModulationWheelRange[nTG] = range;
	m_pTG[nTG]->setMWController(range, m_pTG[nTG]->getModWheelTarget(), 0);
//	m_pTG[nTG]->setModWheelRange(constrain(range, 0, 99));  replaces with the above due to wrong constrain on dexed_synth module. 

	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setModWheelTarget(uint8_t target, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	m_nModulationWheelTarget[nTG] = target;

	m_pTG[nTG]->setModWheelTarget(constrain(target, 0, 7));
	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setFootControllerRange(uint8_t range, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	m_nFootControlRange[nTG]=range;
	m_pTG[nTG]->setFCController(range, m_pTG[nTG]->getFootControllerTarget(), 0);
//	m_pTG[nTG]->setFootControllerRange(constrain(range, 0, 99));  replaces with the above due to wrong constrain on dexed_synth module. 

	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setFootControllerTarget(uint8_t target, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	m_nFootControlTarget[nTG] = target;

	m_pTG[nTG]->setFootControllerTarget(constrain(target, 0, 7));
	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setBreathControllerRange(uint8_t range, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	m_nBreathControlRange[nTG]=range;
	m_pTG[nTG]->setBCController(range, m_pTG[nTG]->getBreathControllerTarget(), 0);
	//m_pTG[nTG]->setBreathControllerRange(constrain(range, 0, 99));

	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setBreathControllerTarget(uint8_t target, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	m_nBreathControlTarget[nTG]=target;

	m_pTG[nTG]->setBreathControllerTarget(constrain(target, 0, 7));
	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setAftertouchRange(uint8_t range, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	m_nAftertouchRange[nTG]=range;
	m_pTG[nTG]->setATController(range, m_pTG[nTG]->getAftertouchTarget(), 0);
//	m_pTG[nTG]->setAftertouchRange(constrain(range, 0, 99));

	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setAftertouchTarget(uint8_t target, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	m_nAftertouchTarget[nTG]=target;

	m_pTG[nTG]->setAftertouchTarget(constrain(target, 0, 7));
	m_pTG[nTG]->ControllersRefresh();
	m_UI.ParameterChanged ();
}

void CMiniDexed::loadVoiceParameters(const uint8_t* data, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	uint8_t voice[161];

	memcpy(voice, data, sizeof(uint8_t)*161);

	// fix voice name
	for (uint8_t i = 0; i < 10; i++)
	{
		if (voice[151 + i] > 126) // filter characters
			voice[151 + i] = 32;
	}

	m_pTG[nTG]->loadVoiceParameters(&voice[6]);
	m_pTG[nTG]->doRefreshVoice();
	m_UI.ParameterChanged ();
}

void CMiniDexed::setVoiceDataElement(uint8_t data, uint8_t number, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	m_pTG[nTG]->setVoiceDataElement(constrain(data, 0, 155),constrain(number, 0, 99));
	//m_pTG[nTG]->doRefreshVoice();
	m_UI.ParameterChanged ();
}

int16_t CMiniDexed::checkSystemExclusive(const uint8_t* pMessage,const  uint16_t nLength, uint8_t nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	return(m_pTG[nTG]->checkSystemExclusive(pMessage, nLength));
}

void CMiniDexed::getSysExVoiceDump(uint8_t* dest, uint8_t nTG)
{
	uint8_t checksum = 0;
	uint8_t data[155];

	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);

	m_pTG[nTG]->getVoiceData(data);

	dest[0] = 0xF0; // SysEx start
	dest[1] = 0x43; // ID=Yamaha
	dest[2] = 0x00 | m_nMIDIChannel[nTG]; // 0x0c Sub-status 0 and MIDI channel
	dest[3] = 0x00; // Format number (0=1 voice)
	dest[4] = 0x01; // Byte count MSB
	dest[5] = 0x1B; // Byte count LSB
	for (uint8_t n = 0; n < 155; n++)
	{
		checksum -= data[n];
		dest[6 + n] = data[n];
	}
	dest[161] = checksum & 0x7f; // Checksum
	dest[162] = 0xF7; // SysEx end
}

void CMiniDexed::setMasterVolume (float32_t vol)
{
	if(vol < 0.0)
		vol = 0.0;
	else if(vol > 1.0)
		vol = 1.0;

	nMasterVolume=vol;
}

std::string CMiniDexed::GetPerformanceFileName(unsigned nID)
{
	return m_PerformanceConfig.GetPerformanceFileName(nID);
}

std::string CMiniDexed::GetPerformanceName(unsigned nID)
{
	return m_PerformanceConfig.GetPerformanceName(nID);
}

unsigned CMiniDexed::GetLastPerformance()
{
	return m_PerformanceConfig.GetLastPerformance();
}



unsigned CMiniDexed::GetActualPerformanceID()
{
	return m_PerformanceConfig.GetActualPerformanceID();
}

void CMiniDexed::SetActualPerformanceID(unsigned nID)
{
	m_PerformanceConfig.SetActualPerformanceID(nID);
}

bool CMiniDexed::SetNewPerformance(unsigned nID)
{
	m_bSetNewPerformance = true;
	m_nSetNewPerformanceID = nID;

	return true;
}

bool CMiniDexed::DoSetNewPerformance (void)
{
	m_bLoadPerformanceBusy = true;
	
	unsigned nID = m_nSetNewPerformanceID;
	m_PerformanceConfig.SetNewPerformance(nID);
	
	if (m_PerformanceConfig.Load ())
	{
		LoadPerformanceParameters();
		m_bLoadPerformanceBusy = false;
		return true;
	}
	else
	{
		SetMIDIChannel (CMIDIDevice::OmniMode, 0);
		m_bLoadPerformanceBusy = false;
		return false;
	}
}

bool CMiniDexed::SavePerformanceNewFile ()
{
	m_bSavePerformanceNewFile = m_PerformanceConfig.GetInternalFolderOk();
	return m_bSavePerformanceNewFile;
}

bool CMiniDexed::DoSavePerformanceNewFile (void)
{
	if (m_PerformanceConfig.CreateNewPerformanceFile())
	{
		if(SavePerformance(false))
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		return false;
	}
	
}


void CMiniDexed::LoadPerformanceParameters(void)
{
	for (unsigned nTG = 0; nTG < CConfig::ToneGenerators; nTG++)
	{
		
		BankSelectLSB (m_PerformanceConfig.GetBankNumber (nTG), nTG);
		ProgramChange (m_PerformanceConfig.GetVoiceNumber (nTG), nTG);
		SetMIDIChannel (m_PerformanceConfig.GetMIDIChannel (nTG), nTG);
		SetVolume (m_PerformanceConfig.GetVolume (nTG), nTG);
		SetPan (m_PerformanceConfig.GetPan (nTG), nTG);
		SetMasterTune (m_PerformanceConfig.GetDetune (nTG), nTG);
		SetCutoff (m_PerformanceConfig.GetCutoff (nTG), nTG);
		SetResonance (m_PerformanceConfig.GetResonance (nTG), nTG);
		setPitchbendRange (m_PerformanceConfig.GetPitchBendRange (nTG), nTG);
		setPitchbendStep (m_PerformanceConfig.GetPitchBendStep (nTG), nTG);
		setPortamentoMode (m_PerformanceConfig.GetPortamentoMode (nTG), nTG);
		setPortamentoGlissando (m_PerformanceConfig.GetPortamentoGlissando  (nTG), nTG);
		setPortamentoTime (m_PerformanceConfig.GetPortamentoTime (nTG), nTG);

		m_nNoteLimitLow[nTG] = m_PerformanceConfig.GetNoteLimitLow (nTG);
		m_nNoteLimitHigh[nTG] = m_PerformanceConfig.GetNoteLimitHigh (nTG);
		m_nNoteShift[nTG] = m_PerformanceConfig.GetNoteShift (nTG);
		
		if(m_PerformanceConfig.VoiceDataFilled(nTG)) 
		{
		uint8_t* tVoiceData = m_PerformanceConfig.GetVoiceDataFromTxt(nTG);
		m_pTG[nTG]->loadVoiceParameters(tVoiceData); 
		}
		setMonoMode(m_PerformanceConfig.GetMonoMode(nTG) ? 1 : 0, nTG); 

#ifdef MIXING_CONSOLE_ENABLE
		for(size_t fx = 0; fx < MixerOutput::kFXCount; ++fx)
		{
			this->setMixingConsoleSendLevel(nTG, static_cast<MixerOutput>(fx), this->m_PerformanceConfig.GetFXSendLevel(nTG, static_cast<MixerOutput>(fx)));
		}
#else
		SetReverbSend (m_PerformanceConfig.GetReverbSend (nTG), nTG);
#endif

		setModWheelRange (m_PerformanceConfig.GetModulationWheelRange (nTG),  nTG);
		setModWheelTarget (m_PerformanceConfig.GetModulationWheelTarget (nTG),  nTG);
		setFootControllerRange (m_PerformanceConfig.GetFootControlRange (nTG),  nTG);
		setFootControllerTarget (m_PerformanceConfig.GetFootControlTarget (nTG),  nTG);
		setBreathControllerRange (m_PerformanceConfig.GetBreathControlRange (nTG),  nTG);
		setBreathControllerTarget (m_PerformanceConfig.GetBreathControlTarget (nTG),  nTG);
		setAftertouchRange (m_PerformanceConfig.GetAftertouchRange (nTG),  nTG);
		setAftertouchTarget (m_PerformanceConfig.GetAftertouchTarget (nTG),  nTG);
	}

#ifdef MIXING_CONSOLE_ENABLE
	this->SetParameter(ParameterFXTubeEnable, this->m_PerformanceConfig.GetFXTubeEnable());
	this->SetParameter(ParameterFXTubeOverdrive, this->m_PerformanceConfig.GetFXTubeOverdrive());
	this->SetParameter(ParameterFXChorusEnable, this->m_PerformanceConfig.GetFXChorusEnable());
	this->SetParameter(ParameterFXChorusRate, this->m_PerformanceConfig.GetFXChorusRate());
	this->SetParameter(ParameterFXChorusDepth, this->m_PerformanceConfig.GetFXChorusDepth());
	this->SetParameter(ParameterFXFlangerEnable, this->m_PerformanceConfig.GetFXFlangerEnable());
	this->SetParameter(ParameterFXFlangerRate, this->m_PerformanceConfig.GetFXFlangerRate());
	this->SetParameter(ParameterFXFlangerDepth, this->m_PerformanceConfig.GetFXFlangerDepth());
	this->SetParameter(ParameterFXFlangerFeedback, this->m_PerformanceConfig.GetFXFlangerFeedback());
	this->SetParameter(ParameterFXOrbitoneEnable, this->m_PerformanceConfig.GetFXOrbitoneEnable());
	this->SetParameter(ParameterFXOrbitoneRate, this->m_PerformanceConfig.GetFXOrbitoneRate());
	this->SetParameter(ParameterFXOrbitoneDepth, this->m_PerformanceConfig.GetFXOrbitoneDepth());
	this->SetParameter(ParameterFXPhaserEnable, this->m_PerformanceConfig.GetFXPhaserEnable());
	this->SetParameter(ParameterFXPhaserRate, this->m_PerformanceConfig.GetFXPhaserRate());
	this->SetParameter(ParameterFXPhaserDepth, this->m_PerformanceConfig.GetFXPhaserDepth());
	this->SetParameter(ParameterFXPhaserFeedback, this->m_PerformanceConfig.GetFXPhaserFeedback());
	this->SetParameter(ParameterFXPhaserNbStages, this->m_PerformanceConfig.GetFXPhaserNbStages());
	this->SetParameter(ParameterFXDelayEnable, this->m_PerformanceConfig.GetFXDelayEnable());
	this->SetParameter(ParameterFXDelayLeftDelayTime, this->m_PerformanceConfig.GetFXDelayLeftDelayTime());
	this->SetParameter(ParameterFXDelayRightDelayTime, this->m_PerformanceConfig.GetFXDelayRightDelayTime());
	this->SetParameter(ParameterFXDelayFeedback, this->m_PerformanceConfig.GetFXDelayFeedback());
	this->SetParameter(ParameterFXReverberatorEnable, this->m_PerformanceConfig.GetFXReverberatorEnable());
	this->SetParameter(ParameterFXReverberatorInputGain, this->m_PerformanceConfig.GetFXReverberatorInputGain());
	this->SetParameter(ParameterFXReverberatorTime, this->m_PerformanceConfig.GetFXReverberatorTime());
	this->SetParameter(ParameterFXReverberatorDiffusion, this->m_PerformanceConfig.GetFXReverberatorDiffusion());
	this->SetParameter(ParameterFXReverberatorLP, this->m_PerformanceConfig.GetFXReverberatorLP());

	size_t end = MixerOutput::kFXCount - 1;
	for(size_t ret = 0; ret < end; ++ret)
	{
		for(size_t fx = 0; fx < MixerOutput::kFXCount; ++fx)
		{
			this->setMixingConsoleReturnLevel(static_cast<MixerOutput>(ret), static_cast<MixerOutput>(fx), this->m_PerformanceConfig.GetFXReturnLevel(static_cast<MixerOutput>(ret), static_cast<MixerOutput>(fx)));
		}
	}
#endif
}

std::string CMiniDexed::GetNewPerformanceDefaultName(void)	
{
	return m_PerformanceConfig.GetNewPerformanceDefaultName();
}

void CMiniDexed::SetNewPerformanceName(std::string nName)
{
	m_PerformanceConfig.SetNewPerformanceName(nName);
}

void CMiniDexed::SetVoiceName (std::string VoiceName, unsigned nTG)
{
	assert (nTG < CConfig::ToneGenerators);
	assert (m_pTG[nTG]);
	char Name[10];
	strncpy(Name, VoiceName.c_str(),10);
	m_pTG[nTG]->getName (Name);
}

bool CMiniDexed::DeletePerformance(unsigned nID)
{
	m_bDeletePerformance = true;
	m_nDeletePerformanceID = nID;

	return true;
}

bool CMiniDexed::DoDeletePerformance(void)
{
	unsigned nID = m_nDeletePerformanceID;
	if(m_PerformanceConfig.DeletePerformance(nID))
	{
		if (m_PerformanceConfig.Load ())
		{
			LoadPerformanceParameters();
			return true;
		}
		else
		{
			SetMIDIChannel (CMIDIDevice::OmniMode, 0);
		}
	}
	
	return false;
}

bool CMiniDexed::GetPerformanceSelectToLoad(void)
{
	return m_pConfig->GetPerformanceSelectToLoad();
}

void CMiniDexed::setModController (unsigned controller, unsigned parameter, uint8_t value, uint8_t nTG)
{
	 uint8_t nBits;
	
	switch (controller)
	{
		case 0:
			if (parameter == 0)
			{
				setModWheelRange(value, nTG);
			}
			else
			{
				value=constrain(value, 0, 1);
				nBits=m_nModulationWheelTarget[nTG];
				value == 1 ?  nBits |= 1 << (parameter-1) : nBits &= ~(1 << (parameter-1)); 
				setModWheelTarget(nBits , nTG); 
			}
		break;
		
		case 1:
			if (parameter == 0)
			{
				setFootControllerRange(value, nTG);
			}
			else
			{
				value=constrain(value, 0, 1);
				nBits=m_nFootControlTarget[nTG];
				value == 1 ?  nBits |= 1 << (parameter-1) : nBits &= ~(1 << (parameter-1)); 
				setFootControllerTarget(nBits , nTG); 
			}
		break;	

		case 2:
			if (parameter == 0)
			{
				setBreathControllerRange(value, nTG);
			}
			else
			{
				value=constrain(value, 0, 1);
				nBits=m_nBreathControlTarget[nTG];
				value == 1 ?  nBits |= 1 << (parameter-1) : nBits &= ~(1 << (parameter-1));
				setBreathControllerTarget(nBits , nTG); 
			}
		break;			
		
		case 3:
			if (parameter == 0)
			{
				setAftertouchRange(value, nTG);
			}
			else
			{
				value=constrain(value, 0, 1);
				nBits=m_nAftertouchTarget[nTG];
				value == 1 ?  nBits |= 1 << (parameter-1) : nBits &= ~(1 << (parameter-1));
				setAftertouchTarget(nBits , nTG); 
			}
		break;	
		default:
		break;
	}
}

unsigned CMiniDexed::getModController (unsigned controller, unsigned parameter, uint8_t nTG)
{
	unsigned nBits;
	switch (controller)
	{
		case 0:
			if (parameter == 0)
			{
			    return m_nModulationWheelRange[nTG];
			}
			else
			{
	
				nBits=m_nModulationWheelTarget[nTG];
				nBits &= 1 << (parameter-1);				
				return (nBits != 0 ? 1 : 0) ; 
			}
		break;
		
		case 1:
			if (parameter == 0)
			{
				return m_nFootControlRange[nTG];
			}
			else
			{
				nBits=m_nFootControlTarget[nTG];
				nBits &= 1 << (parameter-1)	;			
				return (nBits != 0 ? 1 : 0) ; 
			}
		break;	

		case 2:
			if (parameter == 0)
			{
				return m_nBreathControlRange[nTG];
			}
			else
			{
				nBits=m_nBreathControlTarget[nTG];	
				nBits &= 1 << (parameter-1)	;			
				return (nBits != 0 ? 1 : 0) ; 
			}
		break;			
		
		case 3:
			if (parameter == 0)
			{
				return m_nAftertouchRange[nTG];
			}
			else
			{
				nBits=m_nAftertouchTarget[nTG];
				nBits &= 1 << (parameter-1)	;			
				return (nBits != 0 ? 1 : 0) ; 
			}
		break;	
		
		default:
			return 0;
		break;
	}
	
}
