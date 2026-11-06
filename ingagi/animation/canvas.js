// Baked Ingagi timeline: frame 0 is t=0; the last frame holds for 1/fps.
// Exposed for embedding and deterministic seek, with one RAF owner.
const canvas = document.getElementById('animation');
const ctx = canvas.getContext('2d');
const slider = document.getElementById('seek');
const button = document.getElementById('play');
const label = document.getElementById('frame');
slider.max = animation.frames.length - 1;
let frame = 0, playing = false, origin = 0, request = 0;
const rgba = c => `rgba(${c & 255},${(c >>> 8) & 255},${(c >>> 16) & 255},${(c >>> 24) / 255})`;
function render(index) {
    frame = Math.max(0, Math.min(animation.frames.length - 1, Math.floor(index)));
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    const shapes = (animation.frames[frame] || []).slice().sort((a, b) => a.layer - b.layer);
    for (const s of shapes) {
        const a = s.points[0], b = s.points[s.points.length - 1];
        ctx.beginPath();
        if (s.kind === 2) ctx.rect(Math.min(a.X,b.X),Math.min(a.Y,b.Y),Math.abs(b.X-a.X),Math.abs(b.Y-a.Y));
        else if (s.kind === 3) ctx.ellipse((a.X+b.X)/2,(a.Y+b.Y)/2,Math.abs(b.X-a.X)/2,Math.abs(b.Y-a.Y)/2,0,0,2*Math.PI);
        else {
            ctx.moveTo(a.X,a.Y);
            for (let i=1;i<s.points.length;i++) ctx.lineTo(s.points[i].X,s.points[i].Y);
            if (s.kind === 4) ctx.closePath();
        }
        ctx.lineWidth=s.width; ctx.lineJoin='round'; ctx.lineCap='round';
        if (s.kind >= 2 && (s.fill >>> 24)) {ctx.fillStyle=rgba(s.fill);ctx.fill();}
        if (s.stroke >>> 24) {ctx.strokeStyle=rgba(s.stroke);ctx.stroke();}
    }
    slider.value=frame; label.textContent=`${frame+1} / ${animation.frames.length}`;
}
function tick(now) {
    if (!playing) return;
    render(Math.floor((now-origin)*animation.fps/1000)%animation.frames.length);
    request=requestAnimationFrame(tick);
}
function pause() {playing=false;cancelAnimationFrame(request);button.textContent='Play';}
function play() {if(playing)return;playing=true;origin=performance.now()-frame*1000/animation.fps;button.textContent='Pause';request=requestAnimationFrame(tick);}
function seek(index) {if(!Number.isFinite(index))return;render(index);origin=performance.now()-frame*1000/animation.fps;}
button.onclick=()=>playing?pause():play();
slider.oninput=()=>{pause();seek(Number(slider.value));};
globalThis.ingagiAnimation={play,pause,seek,get frame(){return frame;},fps:animation.fps,frames:animation.frames.length};
render(0);play();
