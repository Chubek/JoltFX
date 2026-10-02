"""Independent ffprobe/decode checks for the shared CLI media export pipeline."""
import array
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import wave

cli, ffmpeg, ffprobe = sys.argv[1:4]
def run(args, **kwargs):
    result = subprocess.run(args, capture_output=True, **kwargs)
    if result.returncode:
        raise RuntimeError(f"Command failed: {args}\n{result.stderr.decode(errors='replace')}")
    return result.stdout
if b"encoded_video: yes" not in run([cli, "capabilities"]):
    print("FFmpeg disabled: independent encode checks skipped")
    sys.exit(77)
with tempfile.TemporaryDirectory(prefix="joltfx-media-", dir=os.environ.get("TMPDIR")) as directory:
    root = Path(directory)
    source = root / "source audio.wav"
    with wave.open(str(source), "wb") as out:
        out.setnchannels(2); out.setsampwidth(2); out.setframerate(44100)
        out.writeframes(b"".join(struct.pack("<hh", round(4096*math.sin(i*2*math.pi*440/44100)),
                                             round(8192*math.sin(i*2*math.pi*660/44100))) for i in range(44100)))
    project = root / "av.jfx"
    def write_project(audio_source):
        project.write_text(f'''size 32 16
fps 30000 1001
track "video"
clip solid 0 30 0.6 0.3 0.15 1 0 0 0 0
track "audio"
clip audio "{audio_source}" 6 24 0 0 0 0 0 0 0 0
clip_audio 1 1 0.5 0 0 24
''')
    write_project(source)
    def export(path, *options):
        run([cli, "export-video", str(project), "-o", str(path), "--frames", "30", *options])
    def probe(path):
        return json.loads(run([ffprobe, "-v", "error", "-count_frames", "-show_streams", "-of", "json", str(path)]))
    def samples(path):
        return array.array("f", run([ffmpeg, "-v", "error", "-i", str(path), "-map", "0:a:0", "-f", "f32le", "-ac", "2", "-ar", "48000", "-"]))
    for suffix, vcodec, acodec in [("mkv", "ffv1", "pcm_s16le"), ("mp4", "mpeg4", "aac"), ("mov", "prores", "pcm_s16le")]:
        target = root / f"output.{suffix}"
        export(target)
        streams = probe(target)["streams"]
        video = next(s for s in streams if s["codec_type"]=="video")
        audio = next(s for s in streams if s["codec_type"]=="audio")
        assert video["codec_name"]==vcodec and audio["codec_name"]==acodec
        assert video["width"]==32 and video["height"]==16 and video["nb_read_frames"]=="30"
        assert video["r_frame_rate"]=="30000/1001" and audio["sample_rate"]=="48000" and audio["channels"]==2
        if "duration" in video: assert abs(float(video["duration"])-1.001)<1e-5
        pcm = samples(target)
        expected_frames = 48048
        assert expected_frames <= len(pcm)//2 <= expected_frames+1024
        start = math.ceil(6*1001*48000/30000)
        assert max(abs(x) for x in pcm[:(start-1024)*2])<1e-5
        region = pcm[(start+1024)*2:(start+4096)*2]
        left = math.sqrt(sum(x*x for x in region[0::2])/(len(region)//2))
        right = math.sqrt(sum(x*x for x in region[1::2])/(len(region)//2))
        assert .035<left<.055 and .15<right<.2, (suffix, left, right)
        if suffix=="mkv":
            rgb = run([ffmpeg,"-v","error","-i",str(target),"-frames:v","1","-f","rawvideo","-pix_fmt","rgba","-"])
            assert rgb[:4]==bytes([153,76,38,255])
    # Source conversion exercises portable decoders; AAC exercises FFmpeg decode.
    for extension, codec in [("flac","flac"),("mp3","libmp3lame"),("m4a","aac")]:
        converted = root / f"input.{extension}"
        run([ffmpeg,"-v","error","-i",str(source),"-c:a",codec,str(converted)])
        write_project(converted)
        target = root / f"decoded-{extension}.mkv"; export(target)
        pcm = samples(target)
        assert max(abs(x) for x in pcm[12000*2:16000*2])>.1
    # Source video with no audio is silent; missing media must preserve output.
    silent = root / "silent.mp4"; write_project(source); export(silent,"--no-audio")
    assert len(probe(silent)["streams"])==1
    project.write_text(f'size 32 16\nfps 30 1\ntrack "video"\nclip video "{silent}" 0 30 0 0 0 0 0 0 0 0\n')
    target = root / "silent-source.mkv"; export(target)
    assert all(x==0 for x in samples(target))
    write_project(root/"missing.wav"); target.write_bytes(b"keep")
    failed = subprocess.run([cli,"export-video",str(project),"-o",str(target),"--frames","30"],capture_output=True)
    assert failed.returncode and target.read_bytes()==b"keep"
    assert not list(root.glob("*.jfx-part-*"))
print("Independent codec, audio decoding, A/V timing and transactional CLI checks passed")
