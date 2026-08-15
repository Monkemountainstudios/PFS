const LEVELS = 5;
const ROOT_MIDI = 60; // C4: tune all source samples to C4 to avoid tuning problems - Basta.
const MIN_MIDI = 36;  // C2
const MAX_MIDI = 84;  // C6
const TRACK_X = [21, 60];
const SAMPLE_FILES = [1,2,3,4,5].map(n => `audio/sound${n}.ogg`);
const FUAP_FILE = 'audio/fuap.ogg';

const tempo = document.getElementById('tempo');
const tempoKnob = document.getElementById('tempoKnob');
const tempoValue = document.getElementById('tempoValue');
const swing = document.getElementById('swing');
const swingValue = document.getElementById('swingValue');
const transport = document.getElementById('transport');
const treesEl = document.getElementById('trees');
const ratchetProb = document.getElementById('ratchetProb');
const ratchetCount = document.getElementById('ratchetCount');
const ratchetProbKnob = document.getElementById('ratchetProbKnob');
const ratchetCountKnob = document.getElementById('ratchetCountKnob');
const ratchetProbValue = document.getElementById('ratchetProbValue');
const ratchetCountValue = document.getElementById('ratchetCountValue');
const ratchetFade = document.getElementById('ratchetFade');
const fuapButton = document.getElementById('fuapButton');
const midiButton=document.getElementById('midiButton');
const midiPanel=document.getElementById('midiPanel');
const midiInput=document.getElementById('midiInput');
const midiStatus=document.getElementById('midiStatus');
const midiBpm=document.getElementById('midiBpm');
const midiPhase=document.getElementById('midiPhase');
const nmidiMode=document.getElementById('nmidiMode');
const nmidiStatus=document.getElementById('nmidiStatus');
const nmidiBpm=document.getElementById('nmidiBpm');
const nmidiSource=document.getElementById('nmidiSource');
const nmidiPacket=document.getElementById('nmidiPacket');

let audioCtx = null;
let master = null;
let outputLimiter = null;
let reverbInput = null;
let reverbReturn = null;
let playing = false;
let fuapBuffer = null;
let fuapMode = false;
let schedulerTimer = null;
let nextBaseStepTime = 0;
let baseStepIndex = 0;
const lookaheadMs = 25;
const scheduleAhead = 0.12;
let midiAccess=null,midiPort=null,midiExternal=false;
let midiPulseCounter=0,midiStepCounter=0,midiLastPulseTime=0,midiIntervals=[],midiClockSeenAt=0,midiHasStartReference=false;
const NMIDI_CHANNEL_NAME='monke-nmidi-v3';
const nmidiChannel=('BroadcastChannel' in window)?new BroadcastChannel(NMIDI_CHANNEL_NAME):null;
let nmidiRole='off',nmidiMasterRunning=false,nmidiPhaseOriginMs=null,nmidiBpmValue=Number(tempo.value);
let nmidiLastPacketAt=0,nmidiLastAppliedBpm=null,nmidiLastAppliedOrigin=null,nmidiHeartbeatTimer=null,nmidiPreviousMasterBpm=Number(tempo.value);

const ratchet = { probability: 0.12, repeats: 3, fade: false, routes: [false,false] };

const tracks = Array.from({length:2}, (_, index) => ({
  index,
  open:false,
  muted:false,
  mode:'variation',
  rateDiv:1,
  rateCounter:0,
  level:0,
  nodeIndex:0,
  selectedSample:0,
  buffers:Array(5).fill(null),
  nodes:[],
  branches:[],
  area:null,
  gainNode:null,
  panNode:null,
  filterNode:null,
  dryNode:null,
  sendNode:null,
  gate:0.75,
  volume:1,
  pan:0,
  reverb:0.08,
  filterHz:20000
}));

function midiName(midi) {
  const names = ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'];
  return names[midi % 12] + (Math.floor(midi / 12) - 1);
}

function createTree(track) {
  const area = document.createElement('div');
  area.className = 'tree-area';
  area.style.left = `${TRACK_X[track.index]}%`;
  area.style.transform = 'translateX(-50%) translateY(10px)';
  const svg = document.createElementNS('http://www.w3.org/2000/svg','svg');
  svg.classList.add('branches');
  svg.setAttribute('viewBox','0 0 700 570');
  const nodesLayer = document.createElement('div');
  nodesLayer.className = 'nodes';

  const positions = [];
  const ys = [500, 390, 280, 170, 80];
  for (let level=0; level<LEVELS; level++) {
    const count = 2 ** level;
    positions[level] = [];
    for (let i=0; i<count; i++) {
      const x = ((i + 0.5) / count) * 660 + 20;
      const y = ys[level];
      positions[level][i] = {x,y};
      const b = document.createElement('button');
      b.type = 'button';
      b.className = 'node';
      b.style.left = `${x/7}%`;
      b.style.top = `${y/5.7}%`;
      b.dataset.level = level;
      b.dataset.index = i;
      const state = {active:false, midi: ROOT_MIDI, button:b};
      track.nodes.push(state);
      renderNode(state);
      b.addEventListener('click', () => {
        state.active = !state.active;
        renderNode(state);
        refreshTrackStatus(track);
      });
      b.addEventListener('wheel', e => {
        e.preventDefault();
        state.midi = Math.max(MIN_MIDI, Math.min(MAX_MIDI, state.midi + (e.deltaY < 0 ? 1 : -1)));
        renderNode(state);
      }, {passive:false});
      nodesLayer.appendChild(b);
    }
  }

  for (let level=0; level<LEVELS-1; level++) {
    for (let i=0; i<positions[level].length; i++) {
      for (let child=0; child<2; child++) {
        const p1 = positions[level][i];
        const childIndex = i*2+child;
        const p2 = positions[level+1][childIndex];
        const line = document.createElementNS('http://www.w3.org/2000/svg','line');
        line.setAttribute('x1',p1.x); line.setAttribute('y1',p1.y);
        line.setAttribute('x2',p2.x); line.setAttribute('y2',p2.y);
        line.classList.add('branch'); svg.appendChild(line);
        const pulse = document.createElementNS('http://www.w3.org/2000/svg','line');
        pulse.setAttribute('x1',p1.x); pulse.setAttribute('y1',p1.y);
        pulse.setAttribute('x2',p2.x); pulse.setAttribute('y2',p2.y);
        pulse.classList.add('branch-pulse'); svg.appendChild(pulse);
        track.branches.push({level,parent:i,child:childIndex,pulse});
      }
    }
  }

  const rate = document.createElement('button');
  rate.className = 'rate-button'; rate.type='button'; rate.textContent='1';
  function refreshRateButton() {
    rate.textContent = track.rateDiv === 1 ? '1' : track.rateDiv === 2 ? '½' : '¼';
    rate.classList.toggle('active', track.rateDiv !== 1);
  }
  rate.addEventListener('click', (e) => {
    e.stopPropagation();
    track.rateDiv = track.rateDiv === 1 ? 2 : track.rateDiv === 2 ? 4 : 1;
    track.rateCounter = 0;
    refreshRateButton();
  });
  rate.addEventListener('wheel', (e) => {
    e.preventDefault();
    e.stopPropagation();
    const choices = [1, 2, 4];
    let i = choices.indexOf(track.rateDiv);
    i = e.deltaY < 0 ? Math.min(choices.length - 1, i + 1) : Math.max(0, i - 1);
    track.rateDiv = choices[i];
    track.rateCounter = 0;
    refreshRateButton();
  }, {passive:false});
  area.appendChild(rate);

  const mode = document.createElement('div'); mode.className='tree-mode';
  const staticBtn = document.createElement('button'); staticBtn.textContent='STATIC';
  const varBtn = document.createElement('button'); varBtn.textContent='VARIATION'; varBtn.className='active';
  staticBtn.addEventListener('click',()=>{track.mode='static';staticBtn.classList.add('active');varBtn.classList.remove('active');});
  varBtn.addEventListener('click',()=>{track.mode='variation';varBtn.classList.add('active');staticBtn.classList.remove('active');});
  mode.append(staticBtn,varBtn);

  area.append(svg,nodesLayer,mode); treesEl.appendChild(area); track.area=area;
}

function nodeState(track, level, index) {
  let offset=0; for(let l=0;l<level;l++) offset += 2**l;
  return track.nodes[offset+index];
}
function renderNode(state) {
  state.button.textContent = midiName(state.midi);
  state.button.classList.toggle('active', state.active);
}
function refreshTrackStatus(track) {
  const b = document.querySelector(`.track-button[data-track="${track.index}"]`);
  b.classList.toggle('has-pattern', track.nodes.some(n=>n.active));
}

tracks.forEach(createTree);

document.querySelectorAll('.track-button').forEach(btn => btn.addEventListener('click',()=>{
  const t=tracks[Number(btn.dataset.track)]; t.open=!t.open;
  t.area.classList.toggle('visible',t.open); btn.classList.toggle('tree-open',t.open); btn.setAttribute('aria-pressed',String(t.open));
}));

function ensureAudio() {
  if (audioCtx) return;
  audioCtx = new (window.AudioContext || window.webkitAudioContext)();
  master = audioCtx.createGain(); master.gain.value=0.72;
  outputLimiter = audioCtx.createDynamicsCompressor();
  outputLimiter.threshold.value = -3;
  outputLimiter.knee.value = 0;
  outputLimiter.ratio.value = 20;
  outputLimiter.attack.value = 0.003;
  outputLimiter.release.value = 0.12;
  master.connect(outputLimiter); outputLimiter.connect(audioCtx.destination);
  const convolver=audioCtx.createConvolver();
  convolver.buffer=makeImpulse(1.05,2.4);
  const wet=audioCtx.createGain(); wet.gain.value=1.0;
  reverbInput=audioCtx.createGain(); reverbReturn=wet;
  reverbInput.connect(convolver); convolver.connect(wet); wet.connect(master);
  tracks.forEach(t=>{
    t.filterNode=audioCtx.createBiquadFilter(); t.filterNode.type='lowpass'; t.filterNode.frequency.value=t.filterHz;
    t.gainNode=audioCtx.createGain(); t.gainNode.gain.value=t.volume;
    t.panNode=audioCtx.createStereoPanner(); t.panNode.pan.value=t.pan;
    t.dryNode=audioCtx.createGain(); t.dryNode.gain.value=1;
    t.sendNode=audioCtx.createGain(); t.sendNode.gain.value=t.reverb;
    t.filterNode.connect(t.gainNode); t.gainNode.connect(t.panNode); t.panNode.connect(t.dryNode); t.dryNode.connect(master); t.panNode.connect(t.sendNode); t.sendNode.connect(reverbInput);
  });
  loadSamples();
}
function makeImpulse(seconds, decay){
  const len=Math.floor(audioCtx.sampleRate*seconds), b=audioCtx.createBuffer(2,len,audioCtx.sampleRate);
  for(let c=0;c<2;c++){const d=b.getChannelData(c);for(let i=0;i<len;i++) d[i]=(Math.random()*2-1)*Math.pow(1-i/len,decay);} return b;
}
async function loadSamples(){
  await Promise.all(SAMPLE_FILES.map(async (file,i)=>{
    try { const r=await fetch(file); if(!r.ok) throw new Error(); const b=await audioCtx.decodeAudioData(await r.arrayBuffer()); tracks.forEach(t=>t.buffers[i]=b); }
    catch { console.warn(`Missing ${file}; using synth fallback.`); }
  }));
  try {
    const r = await fetch(FUAP_FILE);
    if (!r.ok) throw new Error();
    fuapBuffer = await audioCtx.decodeAudioData(await r.arrayBuffer());
  } catch {
    console.warn(`Missing ${FUAP_FILE}; FUAP override will use the selected sample.`);
  }
}

function trigger(track, state, time, velocity=1, gateOverride=null) {
  if(track.muted || !state.active) return;
  const stepDur=secondsPerStep()*track.rateDiv;
  const gateTime=(gateOverride ?? track.gate)*stepDur;
  const buffer=(fuapMode && fuapBuffer) ? fuapBuffer : track.buffers[track.selectedSample];
  if(buffer){
    const src=audioCtx.createBufferSource();
    src.buffer=buffer;
    src.playbackRate.value=Math.pow(2,(state.midi-ROOT_MIDI)/12);

    const env=audioCtx.createGain();
    const isFuap = fuapMode && fuapBuffer && buffer === fuapBuffer;

    if(isFuap){
      // FUAP has diplomatic immunity from Gate: let the complete sample play.
      // playbackRate still changes its natural duration when pitched.
      const naturalDuration = buffer.duration / src.playbackRate.value;
      const fadeOut = Math.min(0.025, naturalDuration * 0.1);
      env.gain.setValueAtTime(Math.max(0.0001,velocity),time);
      env.gain.setValueAtTime(Math.max(0.0001,velocity),Math.max(time,time+naturalDuration-fadeOut));
      env.gain.exponentialRampToValueAtTime(0.0001,time+naturalDuration);
      src.connect(env);
      env.connect(track.filterNode);
      src.start(time);
      src.stop(time+naturalDuration+0.03);
    } else {
      env.gain.setValueAtTime(Math.max(0.0001,velocity),time);
      env.gain.setValueAtTime(Math.max(0.0001,velocity),Math.max(time,time+gateTime-0.012));
      env.gain.exponentialRampToValueAtTime(0.0001,time+gateTime);
      src.connect(env);
      env.connect(track.filterNode);
      src.start(time);
      src.stop(time+gateTime+0.03);
    }
  } else synthFallback(track,state,time,velocity,gateTime);
}
function synthFallback(track,state,time,velocity,gateTime){
  // Five deliberately different fallback voices, so SAMPLE 1-5 remains testable
  // even before the real sound files have been added.
  const presets = [
    { type:'sine',     octave:-1, gain:0.20, attack:0.010 },
    { type:'triangle', octave: 0, gain:0.18, attack:0.006 },
    { type:'square',   octave: 0, gain:0.10, attack:0.003 },
    { type:'sawtooth', octave: 1, gain:0.085,attack:0.004 },
    { type:'triangle', octave:-2, gain:0.16, attack:0.018 }
  ];
  const preset = presets[track.selectedSample] || presets[0];
  const osc=audioCtx.createOscillator();
  const env=audioCtx.createGain();
  osc.type=preset.type;
  osc.frequency.value=440*Math.pow(2,((state.midi + preset.octave*12)-69)/12);
  env.gain.setValueAtTime(0.0001,time);
  env.gain.exponentialRampToValueAtTime(preset.gain*velocity,time+preset.attack);
  env.gain.exponentialRampToValueAtTime(0.0001,time+Math.max(.04,gateTime));
  osc.connect(env); env.connect(track.filterNode); osc.start(time); osc.stop(time+Math.max(.06,gateTime+.02));
}
function maybeTrigger(track,state,time){
  const routed=ratchet.routes[track.index];
  if(!routed || Math.random()>=ratchet.probability){ trigger(track,state,time); return; }
  const n=ratchet.repeats; const slot=(secondsPerStep()*track.rateDiv)/n;
  for(let i=0;i<n;i++){
    const v=ratchet.fade ? Math.pow(0.58,i) : 1;
    trigger(track,state,time+i*slot,v,Math.min(track.gate/n,0.95/n));
  }
}

function secondsPerStep(){const bpm=nmidiRole==='follow'?nmidiBpmValue:Number(tempo.value);return 60/bpm/4;}
function scheduler(){
  if(midiExternal) return;
  if(nmidiRole==='follow'&&!nmidiMasterRunning)return;
  while(nextBaseStepTime<audioCtx.currentTime+scheduleAhead){ scheduleBaseStep(nextBaseStepTime,baseStepIndex); baseStepIndex++; nextBaseStepTime += secondsPerStep(); }
}
function scheduleBaseStep(time, idx){
  const swingAmt=(Number(swing.value)-50)/25; const swung=idx%2===1 ? secondsPerStep()*0.45*swingAmt : 0; const eventTime=time+swung;
  tracks.forEach(track=>{
    track.rateCounter++;
    if((track.rateCounter-1)%track.rateDiv!==0) return;
    const level=track.level;
    const state=nodeState(track,level,track.nodeIndex);
    flashNode(state,eventTime); maybeTrigger(track,state,eventTime);
    if(level<LEVELS-1){
      const child=track.mode==='static' ? track.nodeIndex*2 : track.nodeIndex*2+(Math.random()<.5?0:1);
      animateBranch(track,level,track.nodeIndex,child,eventTime,secondsPerStep()*track.rateDiv*0.88);
      track.nodeIndex=child; track.level++;
    } else { track.level=0; track.nodeIndex=0; }
  });
  blinkHiddenTracks(eventTime,idx);
}
function flashNode(state,time){ const delay=Math.max(0,(time-audioCtx.currentTime)*1000); setTimeout(()=>{state.button.classList.add('playhead');setTimeout(()=>state.button.classList.remove('playhead'),90);},delay); }
function animateBranch(track,level,parent,child,time,dur){
  const b=track.branches.find(x=>x.level===level&&x.parent===parent&&x.child===child); if(!b)return;
  const delay=Math.max(0,(time-audioCtx.currentTime)*1000);
  setTimeout(()=>{ const p=b.pulse; p.style.opacity='1'; p.style.strokeDasharray='16 100'; p.style.strokeDashoffset='110'; p.style.transition='none'; requestAnimationFrame(()=>{p.style.transition=`stroke-dashoffset ${Math.max(.04,dur)}s linear, opacity .08s`;p.style.strokeDashoffset='0';}); setTimeout(()=>p.style.opacity='0',dur*1000);},delay);
}
function blinkHiddenTracks(time,idx){ if(idx%4!==0)return; const d=Math.max(0,(time-audioCtx.currentTime)*1000); setTimeout(()=>tracks.forEach(t=>{if(!t.open&&t.nodes.some(n=>n.active)){const b=document.querySelector(`.track-button[data-track="${t.index}"]`);b.classList.add('tempo-flash');setTimeout(()=>b.classList.remove('tempo-flash'),80);}}),d); }

transport.addEventListener('click',async()=>{
  ensureAudio(); await audioCtx.resume();
  playing=!playing; transport.textContent=playing?'STOP':'PLAY'; transport.setAttribute('aria-pressed',String(playing));
  if(playing){
    tracks.forEach(t=>{t.level=0;t.nodeIndex=0;t.rateCounter=0;});
    if(midiExternal){baseStepIndex=midiStepCounter;clearInterval(schedulerTimer);schedulerTimer=null;}
    else if(nmidiRole==='follow'){clearInterval(schedulerTimer);schedulerTimer=null;if(nmidiMasterRunning&&nmidiPhaseOriginMs!=null)startNmidiFollowScheduler();else nmidiStatus.textContent='WAITING';}
    else{
      baseStepIndex=0;nextBaseStepTime=audioCtx.currentTime+.06;
      if(nmidiRole==='master'){nmidiMasterRunning=true;nmidiBpmValue=Number(tempo.value);nmidiPreviousMasterBpm=nmidiBpmValue;nmidiPhaseOriginMs=nmidiGlobalNowMs()+60;sendNmidiState('start');startNmidiHeartbeat();}
      scheduler();schedulerTimer=setInterval(scheduler,lookaheadMs);
    }
  }else{
    clearInterval(schedulerTimer);schedulerTimer=null;
    if(nmidiRole==='master'&&nmidiMasterRunning){nmidiMasterRunning=false;stopNmidiHeartbeat();sendNmidiState('stop');}
  }
});

function bindKnob(knob,input,onInput){
  let startY,startV;
  knob.addEventListener('pointerdown',e=>{if(input===tempo&&(midiExternal||nmidiRole==='follow'))return;startY=e.clientY;startV=Number(input.value);knob.setPointerCapture(e.pointerId);});
  knob.addEventListener('pointermove',e=>{if(input===tempo&&(midiExternal||nmidiRole==='follow'))return;if(startY===undefined)return; const span=Number(input.max)-Number(input.min); input.value=Math.max(Number(input.min),Math.min(Number(input.max),startV+(startY-e.clientY)*span/160)); input.dispatchEvent(new Event('input'));});
  knob.addEventListener('pointerup',()=>{startY=undefined;}); knob.addEventListener('wheel',e=>{if(input===tempo&&(midiExternal||nmidiRole==='follow')){e.preventDefault();return;}e.preventDefault(); input.value=Number(input.value)+(e.deltaY<0?Number(input.step||1):-Number(input.step||1));input.dispatchEvent(new Event('input'));},{passive:false});
  input.addEventListener('input',onInput);
}
function setKnobVisual(knob,input,minDeg=135,sweep=270){ const ind=knob.querySelector('span'); const min=Number(input.min),max=Number(input.max),v=Number(input.value); const t=(v-min)/(max-min); ind.style.transform=`rotate(${minDeg+t*sweep}deg)`; }

bindKnob(tempoKnob,tempo,()=>{
  const newBpm=Math.round(Number(tempo.value));tempoValue.value=`${newBpm} BPM`;setKnobVisual(tempoKnob,tempo);
  if(nmidiRole==='master'){const now=nmidiGlobalNowMs();if(nmidiMasterRunning&&nmidiPhaseOriginMs!=null){const oldStepMs=(60000/nmidiPreviousMasterBpm)/4;const elapsed=(now-nmidiPhaseOriginMs)/oldStepMs;nmidiPhaseOriginMs=now-elapsed*((60000/newBpm)/4);}nmidiPreviousMasterBpm=newBpm;nmidiBpmValue=newBpm;sendNmidiState('tempo');}
}); setKnobVisual(tempoKnob,tempo);
swing.addEventListener('input',()=>swingValue.value=`${swing.value}%`);
bindKnob(ratchetProbKnob,ratchetProb,()=>{ratchet.probability=Number(ratchetProb.value)/100;ratchetProbValue.value=`${Math.round(Number(ratchetProb.value))}%`;setKnobVisual(ratchetProbKnob,ratchetProb);});
bindKnob(ratchetCountKnob,ratchetCount,()=>{ratchet.repeats=Math.round(Number(ratchetCount.value));ratchetCount.value=ratchet.repeats;ratchetCountValue.value=String(ratchet.repeats);setKnobVisual(ratchetCountKnob,ratchetCount);}); setKnobVisual(ratchetProbKnob,ratchetProb); setKnobVisual(ratchetCountKnob,ratchetCount);
ratchetFade.addEventListener('click',()=>{ratchet.fade=!ratchet.fade;ratchetFade.classList.toggle('active',ratchet.fade);ratchetFade.setAttribute('aria-pressed',String(ratchet.fade));});
document.querySelectorAll('.route-button').forEach(b=>b.addEventListener('click',()=>{const i=Number(b.dataset.track);ratchet.routes[i]=!ratchet.routes[i];b.classList.toggle('active',ratchet.routes[i]);b.setAttribute('aria-pressed',String(ratchet.routes[i]));}));

function filterFromSlider(v){return 200*Math.pow(100,v/100);}
document.querySelectorAll('.channel').forEach((ch,i)=>{
  const t=tracks[i];
  ch.querySelectorAll('.sample-button').forEach(b=>b.addEventListener('click',()=>{t.selectedSample=Number(b.dataset.sample);ch.querySelectorAll('.sample-button').forEach(x=>x.classList.toggle('selected',x===b));}));
  const filter=ch.querySelector('.filter'), filterKnob=ch.querySelector('.filter-knob'); bindKnob(filterKnob,filter,()=>{t.filterHz=filterFromSlider(Number(filter.value));if(t.filterNode)t.filterNode.frequency.setTargetAtTime(t.filterHz,audioCtx.currentTime,.01);setKnobVisual(filterKnob,filter);}); setKnobVisual(filterKnob,filter);
  const gate=ch.querySelector('.gate'),vol=ch.querySelector('.volume'),pan=ch.querySelector('.pan'),rev=ch.querySelector('.reverb');
  gate.addEventListener('input',()=>t.gate=Number(gate.value)/100);
  vol.addEventListener('input',()=>{t.volume=Number(vol.value)/100;if(t.gainNode)t.gainNode.gain.setTargetAtTime(t.volume,audioCtx.currentTime,.01);});
  pan.addEventListener('input',()=>{t.pan=Number(pan.value)/100;if(t.panNode)t.panNode.pan.setTargetAtTime(t.pan,audioCtx.currentTime,.01);});
  rev.addEventListener('input',()=>{t.reverb=Number(rev.value)/100;if(t.sendNode)t.sendNode.gain.setTargetAtTime(t.reverb,audioCtx.currentTime,.01);});
});

fuapButton.addEventListener('click',()=>{
  fuapMode = !fuapMode;
  fuapButton.classList.toggle('active',fuapMode);
  fuapButton.setAttribute('aria-pressed',String(fuapMode));
});


/* ---------- WEB MIDI CLOCK IN ---------- */

function nmidiGlobalNowMs(){return performance.timeOrigin+performance.now();}
function sendNmidiState(reason){
  if(!nmidiChannel||nmidiRole!=='master')return;
  nmidiPacket.textContent=String(reason).toUpperCase();nmidiStatus.textContent=nmidiMasterRunning?'MASTER RUN':'MASTER STOP';
  nmidiBpm.textContent=String(Math.round(Number(tempo.value)));nmidiSource.textContent='PFS';
  nmidiChannel.postMessage({protocol:'NMIDI',version:3,type:'state',source:'PFS',bpm:Number(tempo.value),running:nmidiMasterRunning,phaseOriginMs:nmidiPhaseOriginMs,sentAtMs:nmidiGlobalNowMs(),reason});
}
function startNmidiHeartbeat(){stopNmidiHeartbeat();nmidiHeartbeatTimer=setInterval(()=>sendNmidiState('heartbeat'),2000);}
function stopNmidiHeartbeat(){if(nmidiHeartbeatTimer)clearInterval(nmidiHeartbeatTimer);nmidiHeartbeatTimer=null;}
function setPfsTempoFromNmidi(bpm){if(!Number.isFinite(bpm))return;nmidiBpmValue=bpm;const c=Math.max(Number(tempo.min),Math.min(Number(tempo.max),Math.round(bpm)));tempo.value=String(c);tempoValue.value=`${c} BPM`;setKnobVisual(tempoKnob,tempo);nmidiBpm.textContent=String(c);}
function alignPfsToNmidiPhase(){
  if(!audioCtx||nmidiPhaseOriginMs==null)return;
  const stepMs=(60000/nmidiBpmValue)/4, exact=(nmidiGlobalNowMs()-nmidiPhaseOriginMs)/stepMs, whole=Math.floor(exact), frac=exact-whole;
  baseStepIndex=Math.max(0,whole);
  tracks.forEach(t=>{t.rateCounter=Math.max(0,whole);const lw=Math.floor(whole/t.rateDiv);t.level=((lw%LEVELS)+LEVELS)%LEVELS;if(t.level===0)t.nodeIndex=0;});
  nextBaseStepTime=audioCtx.currentTime+Math.max(.006,(1-frac)*secondsPerStep());
}
function startNmidiFollowScheduler(){if(!playing||!audioCtx||nmidiRole!=='follow'||!nmidiMasterRunning)return;alignPfsToNmidiPhase();clearInterval(schedulerTimer);scheduler();schedulerTimer=setInterval(scheduler,lookaheadMs);}
nmidiMode.addEventListener('change',()=>{
  nmidiRole=nmidiMode.value;stopNmidiHeartbeat();
  if(nmidiRole!=='off'&&midiExternal){if(midiPort)midiPort.onmidimessage=null;midiPort=null;midiExternal=false;midiInput.value='';midiStatus.textContent='READY';midiBpm.textContent='--';midiPhase.textContent='--';}
  if(nmidiRole==='off'){nmidiStatus.textContent='OFF';nmidiBpm.textContent='--';nmidiSource.textContent='--';nmidiPacket.textContent='--';nmidiMasterRunning=false;if(playing){baseStepIndex=0;tracks.forEach(t=>{t.level=0;t.nodeIndex=0;t.rateCounter=0;});nextBaseStepTime=audioCtx.currentTime+.05;clearInterval(schedulerTimer);scheduler();schedulerTimer=setInterval(scheduler,lookaheadMs);}return;}
  if(!nmidiChannel){nmidiStatus.textContent='UNAVAILABLE';nmidiMode.value='off';nmidiRole='off';return;}
  if(nmidiRole==='master'){nmidiBpmValue=Number(tempo.value);nmidiPreviousMasterBpm=nmidiBpmValue;nmidiSource.textContent='PFS';nmidiBpm.textContent=String(Math.round(nmidiBpmValue));if(playing){nmidiMasterRunning=true;nmidiPhaseOriginMs=nmidiGlobalNowMs()+60;sendNmidiState('start');startNmidiHeartbeat();}else{nmidiMasterRunning=false;nmidiPhaseOriginMs=null;sendNmidiState('mode');}nmidiStatus.textContent=nmidiMasterRunning?'MASTER RUN':'MASTER STOP';return;}
  if(nmidiRole==='follow'){nmidiStatus.textContent=nmidiLastPacketAt?(nmidiMasterRunning?'FOLLOWING':'MASTER STOP'):'WAITING';clearInterval(schedulerTimer);schedulerTimer=null;if(playing&&nmidiMasterRunning)startNmidiFollowScheduler();}
});
if(nmidiChannel){
  nmidiChannel.onmessage=e=>{
    const m=e.data;if(!m||m.protocol!=='NMIDI'||m.version!==3||m.type!=='state')return;if(nmidiRole==='master'&&m.source==='PFS')return;
    nmidiLastPacketAt=performance.now();nmidiPacket.textContent=String(m.reason||'state').toUpperCase();if(m.source)nmidiSource.textContent=m.source;
    const bpm=Number.isFinite(m.bpm)?m.bpm:nmidiBpmValue, origin=Number.isFinite(m.phaseOriginMs)?m.phaseOriginMs:nmidiPhaseOriginMs, was=nmidiMasterRunning;
    const bpmChanged=Number.isFinite(bpm)&&(nmidiLastAppliedBpm==null||Math.abs(bpm-nmidiLastAppliedBpm)>=.5), originChanged=Number.isFinite(origin)&&(nmidiLastAppliedOrigin==null||Math.abs(origin-nmidiLastAppliedOrigin)>5);
    nmidiMasterRunning=!!m.running;if(Number.isFinite(bpm)&&nmidiRole==='follow')setPfsTempoFromNmidi(bpm);if(Number.isFinite(origin))nmidiPhaseOriginMs=origin;
    if(nmidiRole!=='follow'){nmidiLastAppliedBpm=bpm;nmidiLastAppliedOrigin=origin;return;}
    if(!nmidiMasterRunning){nmidiStatus.textContent='MASTER STOP';clearInterval(schedulerTimer);schedulerTimer=null;nmidiLastAppliedBpm=bpm;nmidiLastAppliedOrigin=origin;return;}
    nmidiStatus.textContent='FOLLOWING';
    const hard=m.reason==='start'||(!was&&nmidiMasterRunning)||bpmChanged||(originChanged&&m.reason!=='heartbeat');
    if(playing&&audioCtx){if(hard)startNmidiFollowScheduler();else if(!schedulerTimer){scheduler();schedulerTimer=setInterval(scheduler,lookaheadMs);}}
    nmidiLastAppliedBpm=bpm;nmidiLastAppliedOrigin=origin;
  };
}else{nmidiMode.disabled=true;nmidiStatus.textContent='UNAVAILABLE';}
setInterval(()=>{if(nmidiRole==='follow'&&nmidiLastPacketAt){const age=performance.now()-nmidiLastPacketAt;if(age>5000)nmidiStatus.textContent='STALE';else if(nmidiMasterRunning)nmidiStatus.textContent='FOLLOWING';}},250);

midiButton.addEventListener('click',async()=>{midiPanel.classList.toggle('open');if(!midiAccess)await initMIDI();});
async function initMIDI(){
  if(!navigator.requestMIDIAccess){midiStatus.textContent='UNAVAILABLE';return;}
  try{midiStatus.textContent='REQUESTING';midiAccess=await navigator.requestMIDIAccess({sysex:false});midiAccess.onstatechange=populateMidiInputs;populateMidiInputs();midiStatus.textContent='READY';}
  catch(err){console.warn('MIDI access failed',err);midiStatus.textContent='DENIED';}
}
function populateMidiInputs(){
  const old=midiInput.value;midiInput.innerHTML='<option value="">NO MIDI INPUT</option>';if(!midiAccess)return;
  for(const input of midiAccess.inputs.values()){const o=document.createElement('option');o.value=input.id;o.textContent=input.name||'MIDI INPUT';midiInput.appendChild(o);}
  if([...midiInput.options].some(o=>o.value===old))midiInput.value=old;
}
midiInput.addEventListener('change',async()=>{
  if(midiPort)midiPort.onmidimessage=null;midiPort=null;midiExternal=!!midiInput.value;
  if(midiExternal&&nmidiRole!=='off'){stopNmidiHeartbeat();nmidiRole='off';nmidiMode.value='off';nmidiStatus.textContent='OFF';nmidiBpm.textContent='--';nmidiSource.textContent='--';nmidiPacket.textContent='--';}
  midiLastPulseTime=0;midiIntervals=[];midiClockSeenAt=0;midiBpm.textContent='--';
  if(!midiExternal){midiStatus.textContent='READY';midiPhase.textContent='--';if(playing){baseStepIndex=0;nextBaseStepTime=audioCtx.currentTime+0.05;clearInterval(schedulerTimer);schedulerTimer=setInterval(scheduler,lookaheadMs);}return;}
  midiPort=midiAccess.inputs.get(midiInput.value);if(midiPort){try{await midiPort.open();}catch(e){}midiPort.onmidimessage=handleMidiMessage;midiStatus.textContent='WAITING';clearInterval(schedulerTimer);schedulerTimer=null;}
});
function updateMidiTempoDisplay(bpm){const min=Number(tempo.min),max=Number(tempo.max),v=Math.max(min,Math.min(max,Math.round(bpm)));tempo.value=String(v);tempoValue.value=`${v} BPM`;setKnobVisual(tempoKnob,tempo);midiBpm.textContent=String(v);}
function flashMidiQuarter(){midiButton.classList.add('clock-pulse');setTimeout(()=>midiButton.classList.remove('clock-pulse'),70);}
function updateMidiPhaseDisplay(){const p=((midiPulseCounter%96)+96)%96,beat=Math.floor(p/24)+1,sixteenth=Math.floor((p%24)/6)+1;midiPhase.textContent=(midiHasStartReference?'':'~')+beat+'.'+sixteenth;}
function handleMidiMessage(e){
  if(!e.data||!e.data.length)return;const status=e.data[0];
  if(status===0xFA){midiPulseCounter=0;midiStepCounter=0;midiHasStartReference=true;midiStatus.textContent='START / CLOCK';updateMidiPhaseDisplay();return;}
  if(status===0xFC){midiStatus.textContent='MASTER STOP';return;}
  if(status!==0xF8)return;
  const now=performance.now();midiClockSeenAt=now;
  if(midiLastPulseTime){const dt=now-midiLastPulseTime;if(dt>1&&dt<1000){midiIntervals.push(dt);if(midiIntervals.length>24)midiIntervals.shift();if(midiIntervals.length>=6){const avg=midiIntervals.reduce((a,b)=>a+b,0)/midiIntervals.length,bpm=60000/(avg*24);if(Number.isFinite(bpm)&&bpm>=20&&bpm<=300)updateMidiTempoDisplay(bpm);}}}
  midiLastPulseTime=now;midiStatus.textContent='CLOCK';if(midiPulseCounter%24===0)flashMidiQuarter();
  if(midiPulseCounter%6===0){const idx=midiStepCounter;if(playing){ensureAudio();scheduleBaseStep(audioCtx.currentTime+0.006,idx);baseStepIndex=idx+1;}midiStepCounter++;}
  midiPulseCounter++;updateMidiPhaseDisplay();
}
setInterval(()=>{if(midiExternal&&midiClockSeenAt&&performance.now()-midiClockSeenAt>1000){midiStatus.textContent='NO CLOCK';midiBpm.textContent='--';}},250);

document.querySelectorAll('.mute-button').forEach(b=>b.addEventListener('click',()=>{const t=tracks[Number(b.dataset.track)];t.muted=!t.muted;b.classList.toggle('active',t.muted);b.setAttribute('aria-pressed',String(t.muted));}));
