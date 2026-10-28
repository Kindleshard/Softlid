# Speaks the guided breaks' and the tour's lines, once, into small mono MP3s in res\voice: with Chatterbox Multilingual
# (Resemble AI, MIT) in its own voice, so BlinkBuddy sounds like one person in every language, and with Piper for the
# four languages Chatterbox doesn't speak (Czech, Hungarian, Romanian, Ukrainian). It runs on the graphics card, in a
# Python that has Chatterbox from its GitHub repository and Piper:
#   uv venv --python 3.11 E:\AI\tts-lab\chatterbox
#   uv pip install --python E:\AI\tts-lab\chatterbox\Scripts\python.exe --extra-index-url https://download.pytorch.org/whl/cu126 git+https://github.com/resemble-ai/chatterbox piper-tts==1.8.0
# with HF_HOME pointing where Chatterbox may be downloaded to (about 3 GB), and the Piper voices named in PIPER (from
# https://huggingface.co/rhasspy/piper-voices) in PIPER_MODELS:
#   make-voices.py                     every language
#   make-voices.py tr en               just these
#   make-voices.py --only far,done tr  just these lines
#   make-voices.py --encode            only compress again, from the recordings kept in build\voice-raw
# Each clip: silence trimmed, a little quiet at both ends, loudness evened out, a small mono MP3 (HZ, KBPS).
import os, pathlib, subprocess, sys, wave

CPP = pathlib.Path(__file__).resolve().parent.parent
FFMPEG = os.environ.get('FFMPEG', r'C:\Tools\ffmpeg\ffmpeg.exe')
RAW = CPP / 'build' / 'voice-raw'   # the recordings as spoken, before compressing
HZ, KBPS = 24000, 32   # (lower was tried: 24 kbit/s already sounds muffled)

# The English text of each line, as the program passes it to Lang::T (the translations are found by it in res\lang).
LINES = {
    'far': "Look away from the screen, at something far away, and let your eyes soften.",
    'sides': "Slowly look left and right, keeping your head still.",
    'updown': "Now slowly look up and down.",
    'circle': "Roll your eyes in a slow, wide circle.",
    'close': "Close your eyes gently, then blink slowly a few times.",
    'done': "Well done. You can look back now.",
    'stretch_intro': "Time to stand up. Follow along with a few easy stretches.",
    'stretch_reach': "Reach both arms up high, and stretch tall.",
    'stretch_shoulders': "Roll your shoulders slowly backwards.",
    'stretch_neck': "Gently tilt your head toward each shoulder.",
    'stretch_twist': "Twist your upper body slowly, side to side.",
    'stretch_chest': "Open your arms wide, and squeeze your shoulder blades together.",
    'stretch_march': "March in place, lifting your knees.",
    'stretch_calves': "Rise up onto your toes, then lower slowly.",
    'stretch_fold': "Bend your knees a little, and hang forward to stretch your back.",
    'stretch_done': "Great job. Have a sip of water, and back to it.",
    'tour_welcome': "Welcome to BlinkBuddy. Gentle breaks and a calmer screen while you work. This takes about a minute.",
    'tour_pick': "Pick what helps you. You can change all of it later.",
    'tour_tray': "BlinkBuddy waits in the tray by the clock. Click it for settings, right-click for quick actions like pausing.",
    'tour_done': "You're all set. Want to see what a break looks like? Try one now, or finish the tour.",
}
# The eye break's lines must fit a 20 s break, so they have less quiet around them.
LOOKAWAY = {'far', 'sides', 'updown', 'circle', 'close', 'done'}

LANGUAGES = ['da', 'de', 'el', 'en', 'es', 'fi', 'fr', 'it', 'ja', 'ko', 'nl', 'pl', 'pt', 'ru', 'sv', 'tr', 'zh']
# Takes other than the first that were kept: a more natural pace, or (Finnish) an eye break that fits its 20 s.
SEEDS = {('en', 'updown'): 3, ('zh', 'done'): 3, ('fi', 'far'): 3, ('fi', 'sides'): 2, ('fi', 'updown'): 2,
         ('fi', 'circle'): 4, ('fi', 'close'): 4, ('fi', 'done'): 2}

PIPER = {'cs': 'cs_CZ-kasandra-medium', 'hu': 'hu_HU-anna-medium', 'ro': 'ro_RO-mihai-medium', 'uk': 'uk_UA-tetiana-high'}
PIPER_MODELS = pathlib.Path(os.environ.get('PIPER_MODELS', r'E:\AI\tts-lab\piper'))
PIPER_PACE = 1.12   # a little slower than Piper's own pace for the stretches and the tour; the eye break at its own


def texts(code):
    def read(c):
        raw = (CPP / 'res' / 'lang' / f'{c}.txt').read_bytes().removeprefix(b'\xef\xbb\xbf')
        return raw.decode('utf-8').replace('\r\n', '\n').split('\n')
    en, other = read('en'), read(code)
    out = {}
    for key, line in LINES.items():
        if line not in en: sys.exit(f'not in en.txt: {line!r}')
        out[key] = other[en.index(line)]
    return out


def encode(src, dst, pad, hz=HZ, kbps=KBPS):
    trim = ('silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.05,areverse,'
            'silenceremove=start_periods=1:start_threshold=-50dB:start_silence=0.05,areverse,'
            f'adelay={int(pad * 1000)}|{int(pad * 1000)},apad=pad_dur={pad},loudnorm=I=-18:TP=-2:LRA=11')
    subprocess.run([FFMPEG, '-v', 'error', '-y', '-i', str(src), '-af', trim, '-ar', str(hz), '-ac', '1', '-c:a', 'libmp3lame',
                    '-b:a', f'{kbps}k', '-map_metadata', '-1', '-id3v2_version', '0', '-write_id3v1', '0', str(dst)], check=True)


def seconds(mp3):
    out = subprocess.run([FFMPEG, '-i', str(mp3)], capture_output=True, text=True).stderr
    h, m, s = out.split('Duration: ')[1].split(',')[0].split(':')
    return int(h) * 3600 + int(m) * 60 + float(s)


class Speakers:
    # The models, loaded the first time each is needed.
    def __init__(self): self.chatterbox, self.piper = None, {}

    def record(self, code, key, text, wav):
        wav.parent.mkdir(parents=True, exist_ok=True)
        if code in PIPER:
            from piper import PiperVoice, SynthesisConfig
            if code not in self.piper: self.piper[code] = PiperVoice.load(str(PIPER_MODELS / f'{PIPER[code]}.onnx'))
            with wave.open(str(wav), 'wb') as w:
                self.piper[code].synthesize_wav(text, w, syn_config=SynthesisConfig(length_scale=1.0 if key in LOOKAWAY else PIPER_PACE))
            return
        import torch, torchaudio
        if not self.chatterbox:
            from chatterbox.mtl_tts import ChatterboxMultilingualTTS
            self.chatterbox = ChatterboxMultilingualTTS.from_pretrained(device='cuda', t3_model='v3')
        torch.manual_seed(SEEDS.get((code, key), 1))   # (the same take each time it's run)
        torchaudio.save(str(wav), self.chatterbox.generate(text, language_id=code), self.chatterbox.sr)


def main():
    args = sys.argv[1:]
    again = '--encode' in args
    if again: args.remove('--encode')
    only = None
    if '--only' in args:
        i = args.index('--only'); only = set(args[i + 1].split(',')); del args[i:i + 2]
    speakers = Speakers()
    for code in args or LANGUAGES + list(PIPER):
        out = CPP / 'res' / 'voice' / code
        out.mkdir(parents=True, exist_ok=True)
        for key, text in texts(code).items():
            if only and key not in only: continue
            wav = RAW / code / f'{key}.wav'
            if not again: speakers.record(code, key, text, wav)
            encode(wav, out / f'{key}.mp3', 0.08 if key in LOOKAWAY else 0.15)
            print(f'  {code} {key} {seconds(out / f"{key}.mp3"):.1f} s', flush=True)
        if all((out / f'{k}.mp3').exists() for k in LOOKAWAY):
            eyes = sum(seconds(out / f'{k}.mp3') for k in LOOKAWAY)
            print(f'{code} done: the eye break speaks for {eyes:.1f} s', flush=True)


if __name__ == '__main__':
    main()
