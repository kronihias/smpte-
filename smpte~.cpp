// smpte~ - Pd/Max (flext) external for generating or decoding ltc audio timecode

// using libltc library by Robin Gareus.
// based on Max object by Mattijs Kneppers

// by Matthias Kronlachner
// www.matthiaskronlachner.com



#include "ltc.h"
#include "decoder.h"
#include "encoder.h"
#include <math.h>

#define FLEXT_ATTRIBUTES 1

#include <flext.h>

#if !defined(FLEXT_VERSION) || (FLEXT_VERSION < 401)
#error You need at least flext version 0.4.1
#endif


class smpte:
	public flext_dsp
{
	
	FLEXT_HEADER(smpte, flext_dsp)

	public:
    
    smpte();
    ~smpte();
		
	
	protected:
    
		virtual void m_signal(int n, float *const *in, float *const *out);
    
        void setFps(int value);
        void setAutoincrease(int value);
        void setTime(int argc,t_atom *argv);
        void setMilliseconds(float f);
    
    
	private:
		
        FLEXT_CALLBACK_I(setFps)
        FLEXT_CALLBACK_I(setAutoincrease)
        FLEXT_CALLBACK_V(setTime)
        FLEXT_CALLBACK_F(setMilliseconds)
    
        LTCEncoder		*encoder;
        LTCDecoder      *decoder;
        double			length; // in seconds
        double			fps;
        double			sampleRate;
        ltcsnd_sample_t *smpteBuffer;
        int				smpteBufferLength;
        int				smpteBufferTime;
        SMPTETimecode	startTimeCode;
        int				startTimeCodeChanged;
        int				autoIncrease;
    
        LTCFrameExt     frame;
        unsigned int    dec_bufpos;
        float           *dec_buffer; // has to contain 256 samples...
    
}; // end of class declaration for smpte



FLEXT_NEW_DSP("smpte~", smpte)

// constructor
smpte::smpte() : fps(25), length(20), autoIncrease(1), startTimeCodeChanged(1), smpteBufferTime(0), smpteBufferLength(0)
{
    
    AddInSignal("timecode input");       // audio timecode in
    
    AddInAnything("jump to millisecond");    // 1 message in
    AddOutSignal("audio out");          // 1 audio out
    
    
    FLEXT_ADDMETHOD_I(1, "fps", setFps);
    FLEXT_ADDMETHOD_I(1, "autoincrease", setAutoincrease);
    FLEXT_ADDMETHOD_(1, "time", setTime);
    FLEXT_ADDMETHOD(1, setMilliseconds);
    
    // SMPTETimecode *st = (SMPTETimecode *)malloc(sizeof(SMPTETimecode));
    const char timezone[6] = "+0100";
    strcpy(startTimeCode.timezone, timezone);
    startTimeCode.years = 0;
    startTimeCode.months = 0;
    startTimeCode.days = 0;
    startTimeCode.hours = 0;
    startTimeCode.mins = 0;
    startTimeCode.secs = 0;
    startTimeCode.frame = 0;
    
    //startTimeCode = st;
    
    encoder = ltc_encoder_create(1, 1, LTC_TV_625_50, 0);
    
    ltc_encoder_set_bufsize(encoder, Samplerate(), fps);
    ltc_encoder_reinit(encoder, Samplerate(), fps, fps == 25 ? LTC_TV_625_50 : LTC_TV_525_60, 0);
    
    ltc_encoder_set_filter(encoder, 0);
    ltc_encoder_set_filter(encoder, 25.0);
    ltc_encoder_set_volume(encoder, -3.0);
    
    // decoder
    int apv = Samplerate()*1/25;
    
    dec_bufpos = 0;
        
    dec_buffer = (float*) malloc(sizeof(float)*256);// allocate buffer

    
    decoder = ltc_decoder_create(apv, 32);
    
    
} // end of constructor

smpte::~smpte ()
{
    ltc_decoder_free(decoder);
    ltc_encoder_free(encoder);
    
    free(dec_buffer);
    
}

void smpte::setFps(int value)
{
    
    switch (value) {
		case 0:
			fps = 24;
			break;
		case 1:
			fps = 25;
			break;
		case 2:
			fps = 29.97;
			break;
		case 3:
			fps = 30;
			break;
		default:
			break;
	}
	ltc_encoder_set_bufsize(encoder, Samplerate(), fps);
	ltc_encoder_reinit(encoder, Samplerate(), fps, fps == 25 ? LTC_TV_625_50 : LTC_TV_525_60, 0);
    
}


void smpte::setAutoincrease(int value)
{
    
    if (value <= 0) value = 0;
	if (value >= 1) value = 1;
	autoIncrease = value;
    
}

void smpte::setTime(int argc,t_atom *argv)
{
    if (argc != 4) {
        post("time: please pass a list with four numbers");
    }
    else
    {
        if (GetInt(argv[3]) > fps)
        {
            post ("requested frame number higher than fps");
        } else {
            
            startTimeCode.hours = GetInt(argv[0]);
            startTimeCode.mins = GetInt(argv[1]);
            startTimeCode.secs = GetInt(argv[2]);
            startTimeCode.frame = GetInt(argv[3]);
            startTimeCodeChanged = 1;
        }
    }
    
    
}

void smpte::setMilliseconds(float f)
{
    double timeInSeconds = f / 1000.;
    double intPart = 0;
    double subSecond = modf(timeInSeconds, &intPart);
    
    startTimeCode.hours = timeInSeconds / 360;
    startTimeCode.mins = (int)(timeInSeconds / 60) % 60;
    startTimeCode.secs = (int)(timeInSeconds) % 60;
    startTimeCode.frame = (int)(subSecond * fps);
    
    startTimeCodeChanged = 1;
    // post("jumped to %i", f);
}

void smpte::m_signal(int n, float *const *in, float *const *out)
{
	
	float *inp =  in[0];
    	
	float *outp = out[0];
    
    
    // check if timecode signal at input
    bool ltc_input = false;
    
    float* input = inp;
    
    // search for first non-zero sample
    
    for (int i = 0; i < n; i++) {
        if ((*input++) != 0.f) {
            ltc_input = true;
            break;
        }
    }
    
    if (ltc_input) { // get timecode from audio signal
        
        for (int i = 0; i < n; i++) {
            if (dec_bufpos > 255)
            {
                ltc_decoder_write_float(decoder, dec_buffer, 256, 0);
                dec_bufpos = 0;
            }
            
            dec_buffer[dec_bufpos++] = inp[i];
        }
        
        
        while (ltc_decoder_read(decoder, &frame)) {
            SMPTETimecode stime;
            ltc_frame_to_time(&stime, &frame.ltc, 1);
            
            // output parts of timecode individually
            AtomList timecode_list(4);
            SetFloat(timecode_list[0], stime.hours);
            SetFloat(timecode_list[1], stime.mins);
            SetFloat(timecode_list[2], stime.secs);
            SetFloat(timecode_list[3], stime.frame);
            
            ToOutList(1, timecode_list);
            
            // store timecode to buffer (if signal is lost stay at this value or increase from there)
            startTimeCode = stime;
            startTimeCodeChanged = 1;
            
            /*
             printf("%04d-%02d-%02d %s ",
             ((stime.years < 67) ? 2000+stime.years : 1900+stime.years),
             stime.months,
             stime.days,
             stime.timezone
             );
             printf("%02d:%02d:%02d%c%02d | %8lld %8lld%s\n",
             stime.hours,
             stime.mins,
             stime.secs,
             (frame.ltc.dfbit) ? '.' : ':',
             stime.frame,
             frame.off_start,
             frame.off_end,
             frame.reverse ? " R" : ""
             );
             */

        }
        
    }
    else // generate timecode
    {
        while (n--) {
            if (smpteBufferTime >= smpteBufferLength) {
                if (startTimeCodeChanged) {
                    ltc_encoder_set_timecode(encoder, &startTimeCode);
                    startTimeCodeChanged = 0;
                    
                }
                else if (autoIncrease) {
                    ltc_encoder_inc_timecode(encoder);
                }
                else {
                    //user apparently wants to keep using the same frame twice
                }
                
                SMPTETimecode st;
                ltc_encoder_get_timecode(encoder, &st);
                
                char timeString[256];
                sprintf(timeString, "%02d:%02d:%02d:%02d", st.hours, st.mins, st.secs, st.frame);
                
                // output parts of timecode individually
                AtomList timecode_list(4);
                SetFloat(timecode_list[0], st.hours);
                SetFloat(timecode_list[1], st.mins);
                SetFloat(timecode_list[2], st.secs);
                SetFloat(timecode_list[3], st.frame);
                
                ToOutList(1, timecode_list);
                
                ltc_encoder_encode_frame(encoder);
                
                smpteBuffer = ltc_encoder_get_bufptr(encoder, &smpteBufferLength, 1);
                smpteBufferTime = 0;
            }
            
            *outp++ = smpteBuffer[smpteBufferTime] / 128. - 1.;
            
            smpteBufferTime++;
        }
    }

}  // end m_signal
