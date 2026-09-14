

// ===================================================
// CANVAS
// ===================================================

const canvas =
document.getElementById("wave");

const ctx =
canvas.getContext("2d");


// ===================================================
// WAVEFORM SETTINGS & INTERACTION
// ===================================================

let currentData = [];
let viewMultiplier = 1;
let displayMode = "sine"; // EPDI always displays the measured signal as one clean sine wave
let lastFrequency = 0;
let fixedRange = 0;
let viewCenter = 0.5;
let holdWaveform = false;
let showGrid = true;
let activeDevice = "device_1";
let cursorX = -1;
let dragActive = false;
let dragStartX = 0;
let dragStartCenter = 0.5;
setTimeout(()=>setDisplayMode("sine"),0);

function clamp(v, a, b){ return Math.max(a, Math.min(b, v)); }

function setView(multiplier)
{
    viewMultiplier = clamp(multiplier, 0.03125, 16);
    viewCenter = 0.5;
    drawWave(currentData);
}

function zoomIn()
{
    setView(Math.min(16, viewMultiplier * 2));
}

function zoomOut()
{
    setView(Math.max(0.03125, viewMultiplier / 2));
}

function resetView()
{
    viewMultiplier = 1;
    viewCenter = 0.5;
    cursorX = -1;
    drawWave(currentData);
}


function fitSine(data, frequency)
{
    if(!data || data.length < 8 || !frequency || frequency <= 0) return null;

    let mean = 0;
    for(let i=0;i<data.length;i++) mean += Number(data[i]);
    mean /= data.length;

    const w = 2*Math.PI*frequency/4000.0;
    let ss = 0, cc = 0, sc = 0, ys = 0, yc = 0;
    for(let i=0;i<data.length;i++){
        const si = Math.sin(w*i);
        const co = Math.cos(w*i);
        const y = Number(data[i]) - mean;
        ss += si*si; cc += co*co; sc += si*co;
        ys += y*si; yc += y*co;
    }

    const det = ss*cc-sc*sc;
    if(Math.abs(det) < 1e-9) return null;

    const a = (ys*cc-yc*sc)/det;
    const b = (yc*ss-ys*sc)/det;
    let amplitude = Math.sqrt(a*a+b*b);

    // For a rectified/clamped input, use the measured peak-to-peak
    // amplitude when the correlation fit becomes too small.
    let measuredMin = Number(data[0]), measuredMax = Number(data[0]);
    for(let i=1;i<data.length;i++){
        const v=Number(data[i]);
        if(v<measuredMin) measuredMin=v;
        if(v>measuredMax) measuredMax=v;
    }
    const measuredAmp = Math.max(0.005,(measuredMax-measuredMin)/2);
    if(amplitude < measuredAmp*0.35) amplitude = measuredAmp;
    amplitude = Math.max(amplitude, measuredAmp);

    const phase = Math.atan2(b,a);
    return {offset:mean, amplitude:amplitude, phase:phase, frequency:frequency};
}

function sineValue(model, tSeconds)
{
    return model.amplitude*Math.sin(2*Math.PI*model.frequency*tSeconds + model.phase);
}

function toggleHold()
{
    holdWaveform = !holdWaveform;
    document.getElementById("holdBtn").innerText = holdWaveform ? "RESUME" : "HOLD";
}

function toggleGrid()
{
    showGrid = !showGrid;
    document.getElementById("gridBtn").innerText = showGrid ? "GRID ON" : "GRID OFF";
    drawWave(currentData);
}

function setVertical(range)
{
    fixedRange = range;
    drawWave(currentData);
}

function autoScale()
{
    // AUTO Y returns the waveform to the initial oscilloscope view.
    // It restores the original zoom, center position and automatic vertical scale.
    fixedRange = 0;
    viewMultiplier = 1;
    viewCenter = 0.5;
    cursorX = -1;

    if(activeDevice !== "device_1") {
        clearWaveNoSignal();
        return;
    }

    if(currentData && currentData.length >= 2)
        drawWave(currentData, lastFrequency);
    else
        clearWaveNoSignal();
}

function exportWaveCSV()
{
    if(!currentData || currentData.length < 2){ alert("No waveform data available"); return; }
    const sampleRate = 4000;
    let csv = "EPDI Waveform Export\nSample Rate (Hz)," + sampleRate + "\nTime (ms),Voltage (V)\n";
    for(let i=0;i<currentData.length;i++)
        csv += ((i/sampleRate)*1000).toFixed(4) + "," + Number(currentData[i]).toFixed(6) + "\n";
    const blob = new Blob([csv], {type:"text/csv"});
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "EPDI_waveform.csv";
    a.click();
    setTimeout(()=>URL.revokeObjectURL(a.href),1000);
}

function getViewData(data)
{
    if(!data || data.length < 2) return [];
    const n = data.length;
    const span = Math.min(1, 1 / viewMultiplier);
    let start = viewMultiplier >= 1 ? Math.round((viewCenter - span/2) * (n-1)) : 0;
    let end   = viewMultiplier >= 1 ? Math.round((viewCenter + span/2) * (n-1)) : n-1;
    start = clamp(start,0,n-2);
    end = clamp(end,start+1,n-1);
    return data.slice(start,end+1);
}

function clearWaveNoSignal()
{
    currentData = [];
    lastFrequency = 0;
    const W = canvas.width, H = canvas.height;
    const TOP = 35, BOTTOM = 30;
    const GRAPH_HEIGHT = H - TOP - BOTTOM;

    ctx.clearRect(0, 0, W, H);

    // Keep the oscilloscope clean when no signal is present; no warning text is shown.
    if(showGrid){
        ctx.lineWidth = 1;
        ctx.strokeStyle = "#17364a";
        for(let i=0;i<=8;i++){
            let y = TOP + GRAPH_HEIGHT*i/8;
            ctx.beginPath(); ctx.moveTo(0,y); ctx.lineTo(W,y); ctx.stroke();
        }
        for(let i=0;i<=10;i++){
            let x = W*i/10;
            ctx.beginPath(); ctx.moveTo(x,TOP); ctx.lineTo(x,H-BOTTOM); ctx.stroke();
        }
    }

    const centerY = H/2;
    ctx.strokeStyle = "#627887";
    ctx.lineWidth = 1;
    ctx.beginPath(); ctx.moveTo(0,centerY); ctx.lineTo(W,centerY); ctx.stroke();


}

function drawWave(data, frequency)
{
    if(!data || data.length < 2) { clearWaveNoSignal(); return; }
    currentData = data;
    if(frequency && frequency > 0) lastFrequency = frequency;

    const visible = getViewData(data);
    if(visible.length < 2) return;

    const W = canvas.width, H = canvas.height;
    const TOP = 35, BOTTOM = 30, GRAPH_HEIGHT = H - TOP - BOTTOM;
    ctx.clearRect(0,0,W,H);

    const rawMin = Math.min(...visible), rawMax = Math.max(...visible);
    const rawCenter = (rawMax + rawMin)/2;
    const rawAmplitude = Math.max(0.02,(rawMax-rawMin)/2);
    const model = fitSine(data,lastFrequency);

    // Build the display signal. The ONE displayed waveform is a clean sine representation fitted from the
    // measured signal. The underlying raw ADC samples remain unchanged for diagnostics.
    let plotMin, plotMax, center, amplitude;
    if(displayMode === "sine" && model){
        center = 0;
        amplitude = model.amplitude;
        plotMin = center-amplitude;
        plotMax = center+amplitude;
    } else {
        center = rawCenter;
        amplitude = rawAmplitude;
        plotMin = rawMin;
        plotMax = rawMax;
    }

    if(fixedRange > 0) amplitude = fixedRange;
    else amplitude *= 1.15;

    if(showGrid){
        ctx.lineWidth=1; ctx.strokeStyle="#17364a";
        for(let i=0;i<=8;i++){let y=TOP+GRAPH_HEIGHT*i/8;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(W,y);ctx.stroke();}
        for(let i=0;i<=10;i++){let x=W*i/10;ctx.beginPath();ctx.moveTo(x,TOP);ctx.lineTo(x,H-BOTTOM);ctx.stroke();}
    }

    const centerY=H/2;
    ctx.strokeStyle="#627887";ctx.lineWidth=1;
    ctx.beginPath();ctx.moveTo(0,centerY);ctx.lineTo(W,centerY);ctx.stroke();

    function yFromValue(v){
        let normalized=(v-center)/amplitude;
        normalized=clamp(normalized,-1,1);
        return centerY-normalized*(GRAPH_HEIGHT/2);
    }

    // Draw ideal sine across the complete visible time window.
    if(displayMode === "sine" && model){
        const totalSeconds=data.length/4000.0;
        let startFraction=viewMultiplier>=1 ? clamp(viewCenter-0.5/viewMultiplier,0,1) : 0;
        let endFraction=viewMultiplier>=1 ? clamp(viewCenter+0.5/viewMultiplier,0,1) : 1;
        const startTime=startFraction*totalSeconds;
        const endTime=endFraction*totalSeconds;

        // At zoom-out levels, extend the mathematically fitted sine beyond
        // the captured 128 ms buffer so the user can see many complete cycles.
        const displayWindowSeconds=totalSeconds/viewMultiplier;
        const baseStart=viewMultiplier<1 ? 0 : startTime;
        const baseEnd=viewMultiplier<1 ? displayWindowSeconds : endTime;

        ctx.beginPath();
        const samples=Math.max(1200,Math.min(5000,W*4));
        for(let k=0;k<=samples;k++){
            const t=baseStart+(baseEnd-baseStart)*k/samples;
            const y=yFromValue(sineValue(model,t));
            const x=W*k/samples;
            if(k===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
        }
        ctx.strokeStyle="#35ed83";ctx.lineWidth=3;ctx.lineJoin="round";ctx.lineCap="round";ctx.stroke();

        // Overlay a very subtle raw trace only when zoomed in, to show the
        // relationship between measured data and the fitted sine.
        if(viewMultiplier>=1){
            const step=W/(visible.length-1);
            ctx.beginPath();
            for(let i=0;i<visible.length;i++){
                const x=i*step, y=yFromValue(visible[i]);
                if(i===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
            }
            ctx.strokeStyle="rgba(170,180,190,0.28)";ctx.lineWidth=1;ctx.stroke();
        }
    } else {
        // Smooth raw measured signal for diagnostic inspection.
        function getY(i){return yFromValue(visible[clamp(i,0,visible.length-1)]);}
        function catmullRom(p0,p1,p2,p3,t){return 0.5*(2*p1+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t*t+(-p0+3*p1-3*p2+p3)*t*t*t);}
        ctx.beginPath();
        const points=visible.length, step=W/(points-1), subdivisions=8;
        for(let i=0;i<points-1;i++){
            const p0=getY(i-1),p1=getY(i),p2=getY(i+1),p3=getY(i+2);
            for(let j=0;j<subdivisions;j++){
                const t=j/subdivisions,y=catmullRom(p0,p1,p2,p3,t),x=(i+t)*step;
                if(i===0&&j===0)ctx.moveTo(x,y);else ctx.lineTo(x,y);
            }
        }
        ctx.lineTo(W,getY(points-1));
        ctx.strokeStyle="#35ed83";ctx.lineWidth=3;ctx.lineJoin="round";ctx.lineCap="round";ctx.stroke();
    }

    // Reference peak/minimum markers from the measured signal.
    let maxI=0,minI=0;
    for(let i=1;i<visible.length;i++){if(visible[i]>visible[maxI])maxI=i;if(visible[i]<visible[minI])minI=i;}
    const markerStep=W/(visible.length-1);
    ctx.setLineDash([5,5]);
    ctx.strokeStyle="#e9c46a";ctx.beginPath();ctx.moveTo(maxI*markerStep,TOP);ctx.lineTo(maxI*markerStep,H-BOTTOM);ctx.stroke();
    ctx.strokeStyle="#d88bff";ctx.beginPath();ctx.moveTo(minI*markerStep,TOP);ctx.lineTo(minI*markerStep,H-BOTTOM);ctx.stroke();ctx.setLineDash([]);

    if(cursorX>=0){
        ctx.strokeStyle="#fff";ctx.lineWidth=1;ctx.beginPath();ctx.moveTo(cursorX,TOP);ctx.lineTo(cursorX,H-BOTTOM);ctx.stroke();
        const ci=Math.round(cursorX/W*(visible.length-1));
        const cv=visible[clamp(ci,0,visible.length-1)];
        const totalMs=data.length/4000*1000;
        const spanMs=totalMs/Math.max(viewMultiplier,1);
        const timeMs=(Math.max(0,viewCenter-1/(2*Math.max(viewMultiplier,1)))*totalMs)+(ci/(visible.length-1))*spanMs;
        document.getElementById("cursorInfo").innerText=timeMs.toFixed(2)+" ms / "+cv.toFixed(4)+" V";
    }else document.getElementById("cursorInfo").innerText="--";

    ctx.fillStyle="#9ab0bf";ctx.font="13px Arial";
    ctx.fillText((center+amplitude).toFixed(2)+" V",8,20);
    ctx.fillText(center.toFixed(2)+" V",8,centerY-6);
    ctx.fillText((center-amplitude).toFixed(2)+" V",8,H-8);
    ctx.fillText((displayMode==="sine"?"FULL SINE ":"RAW ")+" ZOOM "+viewMultiplier.toFixed(3)+"×",W-190,20);

    document.getElementById("viewInfo").innerText=viewMultiplier.toFixed(3)+"×"+(viewMultiplier===1?" / FULL":(viewMultiplier<1?" / ZOOM OUT":" / PAN"));
    document.getElementById("timeWindow").innerText=(data.length/4000*1000/viewMultiplier).toFixed(2)+" ms";
    if(displayMode==="sine" && model){
        document.getElementById("minInfo").innerText=(-model.amplitude).toFixed(4)+" V";
        document.getElementById("maxInfo").innerText=(model.amplitude).toFixed(4)+" V";
        document.getElementById("viewPP").innerText=(2*model.amplitude).toFixed(4)+" V";
    }else{
        document.getElementById("minInfo").innerText=rawMin.toFixed(4)+" V";
        document.getElementById("maxInfo").innerText=rawMax.toFixed(4)+" V";
        document.getElementById("viewPP").innerText=(rawMax-rawMin).toFixed(4)+" V";
    }
}

canvas.addEventListener("wheel", function(e){
    e.preventDefault();
    if(e.deltaY < 0) zoomIn(); else zoomOut();
});

canvas.addEventListener("mousedown", function(e){
    const r=canvas.getBoundingClientRect();
    dragActive=true;
    dragStartX=e.clientX-r.left;
    dragStartCenter=viewCenter;
    cursorX=dragStartX;
    drawWave(currentData);
});

canvas.addEventListener("mousemove", function(e){
    if(!currentData.length) return;
    const r=canvas.getBoundingClientRect();
    const x=clamp(e.clientX-r.left,0,canvas.width);
    if(dragActive && viewMultiplier>1){
        const delta=(x-dragStartX)/canvas.width*(1/viewMultiplier);
        viewCenter=clamp(dragStartCenter-delta,1/(2*viewMultiplier),1-1/(2*viewMultiplier));
    }
    cursorX=x;
    drawWave(currentData);
});

canvas.addEventListener("mouseup",()=>dragActive=false);
canvas.addEventListener("mouseleave",()=>dragActive=false);

// ===================================================
// ADVANCED DIAGNOSTIC DISPLAY
// ===================================================

function updateAdvancedDiagnostics(d) {
  const vals = {
    dcOffset: d.dcOffset,
    peakVoltage: d.peakVoltage,
    thd: d.thd,
    fundamental: d.fundamental,
    harmonic2: d.harmonic2,
    harmonic3: d.harmonic3,
    rmsPeakRatio: d.rmsPeakRatio,
    kurtosis: d.kurtosis,
    skewness: d.skewness,
    zeroCrossings: d.zeroCrossings,
    frequencyStability: d.frequencyStability,
    phaseStability: d.phaseStability,
    voltageRipple: d.voltageRipple,
    variance: d.variance,
    snrDb: d.snrDb,
    faultIndex: d.faultIndex
  };
  Object.keys(vals).forEach(k => {
    const el = document.getElementById(k);
    if (el) el.textContent = (vals[k] === undefined || vals[k] === null) ? '--' : vals[k];
  });
}

async function updateData()
{
    try
    {

        let response =
            await fetch(
                "/data"
            );


        let d =
            await response.json();

        // =========================================
        // ADVANCED DIAGNOSTIC VALUES
        // =========================================
        updateAdvancedDiagnostics(d);

        // =========================================
        // BASIC VALUES
        // =========================================

        document
        .getElementById("adc")
        .innerText =
            d.adc;


        document
        .getElementById("voltage")
        .innerText =
            d.adcVoltage
            .toFixed(2);


        document
        .getElementById("frequency")
        .innerText =
            d.frequency
            .toFixed(1);


        document
        .getElementById("rpm")
        .innerText =
            Math.round(
                d.rpm
            );


        document
        .getElementById("rms")
        .innerText =
            d.rms
            .toFixed(3);


        document
        .getElementById("pp")
        .innerText =
            d.peakToPeak
            .toFixed(3);


        document
        .getElementById("crest")
        .innerText =
            d.crest
            .toFixed(2);


        // =========================================
        // FEATURES
        // =========================================

        document
        .getElementById("fRms")
        .innerText =
            d.rms.toFixed(3)
            + " V";


        document
        .getElementById("fPP")
        .innerText =
            d.peakToPeak.toFixed(3)
            + " V";


        document
        .getElementById("fCrest")
        .innerText =
            d.crest.toFixed(2);


        document
        .getElementById("fFreq")
        .innerText =
            d.frequency.toFixed(1)
            + " Hz";


        document
        .getElementById("fRPM")
        .innerText =
            Math.round(
                d.rpm
            );


        // =========================================
        // HEALTH
        // =========================================

        document
        .getElementById("health")
        .innerText =
            d.health;


        document
        .getElementById("fault")
        .innerText =
            d.fault;


        // =========================================
        // CONDITION
        // =========================================

        document
        .getElementById("condition")
        .innerText =
            d.condition;


        document
        .getElementById("samples")
        .innerText =
            d.samples;


        document
        .getElementById("storedProfiles")
        .innerText =
            d.storedProfiles;

        document
        .getElementById("testResult")
        .innerText =
            d.testResult;

        document
        .getElementById("testScore")
        .innerText =
            d.testScore.toFixed(1) + "%";

        document
        .getElementById("recording")
        .innerText =
            d.recording ? "RECORDING" : "STOPPED";


        // =========================================
        // WAVEFORM
        // =========================================

        if(!holdWaveform)
        {
            // IMPORTANT: never keep the previous sine visible after the
            // machine/signal has stopped. Frequency is the primary signal
            // presence indicator; RMS also rejects a near-zero input.
            const signalPresent =
                Number(d.frequency) >= 1.0 &&
                Number(d.rms) >= 0.01 &&
                Array.isArray(d.wave) &&
                d.wave.length >= 2;

            // Display live waveform/details only for the currently connected DEVICE_1.
            if(activeDevice === "device_1" && signalPresent)
                drawWave(d.wave, d.frequency);
            else
                clearWaveNoSignal();
        }

    }

    catch(error)
    {

        document
        .getElementById("fault")
        .innerText =
            "CONNECTION ERROR";

    }

}


// ===================================================
// DEVICE SELECTION
// ===================================================
function updateDeviceVisibility()
{
    // DEVICE_1 is the only currently connected acquisition device.
    // Keep the device selector visible, but hide every other dashboard panel
    // whenever another device slot is selected.
    document.querySelectorAll(".panel:not(.devicePanel)").forEach(function(panel){
        panel.style.display = (activeDevice === "device_1") ? "" : "none";
    });

    // Also hide the standalone diagnostic/data blocks that are not wrapped in
    // a .panel in older EPDI layouts.
    const ids = [
        "adc","voltage","frequency","rpm","rms","pp","crest",
        "health","fault","condition","storedProfiles","testResult",
        "testScore","recording","samples"
    ];
    ids.forEach(function(id){
        const el = document.getElementById(id);
        if(el) {
            const holder = el.closest(".panel");
            if(!holder) el.style.display = (activeDevice === "device_1") ? "" : "none";
        }
    });
}

function selectDevice(device)
{
    activeDevice = device;

    // Device selection itself always remains visible.
    if(activeDevice === "device_1") {
        document.getElementById("deviceStatus").innerText = "ONLINE";
        document.getElementById("deviceStatus").className = "deviceStatusConnected";

        // Restore the initial waveform view when DEVICE_1 is selected.
        viewMultiplier = 1;
        viewCenter = 0.5;
        fixedRange = 0;
        cursorX = -1;

        updateDeviceVisibility();

        if(currentData && currentData.length >= 2)
            drawWave(currentData, lastFrequency);
        else
            clearWaveNoSignal();
    } else {
        document.getElementById("deviceStatus").innerText =
            device.toUpperCase() + " OFFLINE";
        document.getElementById("deviceStatus").className = "deviceStatusDisconnected";

        // Immediately remove waveform and all device-specific details.
        currentData = [];
        lastFrequency = 0;
        updateDeviceVisibility();
        clearWaveNoSignal();
    }
}

// Apply DEVICE_1 visibility on initial page load.
setTimeout(updateDeviceVisibility, 0);

// ===================================================
// CUSTOM DIAGNOSTIC OPTIONS
// ===================================================
async function addOtherCondition()
{
    const name = prompt("Enter a new EPDI diagnostic condition:");
    if(!name) return;

    const clean = name.trim().replace(/\s+/g, " ").toUpperCase();
    if(clean.length < 2) return;

    const response = await fetch("/addCondition?name=" + encodeURIComponent(clean));
    const result = await response.text();
    if(!response.ok){ alert(result || "Unable to add diagnostic option"); return; }
    if(result === "Condition already exists"){ alert(result); return; }

    const grid = document.getElementById("conditionGrid");
    const btn = document.createElement("button");
    btn.innerText = clean;
    btn.onclick = () => condition(clean);
    const addBtn = grid.querySelector(".addConditionBtn");
    grid.insertBefore(btn, addBtn);
    condition(clean);
}

// ===================================================
// CONDITION
// ===================================================

async function condition(name)
{

    await fetch(
        "/condition?name=" +
        encodeURIComponent(name)
    );


    document
    .getElementById("condition")
    .innerText =
        name;

}


// ===================================================
// SAVE BASELINE
// ===================================================

async function baseline()
{

    await fetch(
        "/baseline"
    );


    alert(
        "Normal baseline saved"
    );

}


// ===================================================
// START RECORDING
// ===================================================

async function startRecording()
{

    await fetch(
        "/start"
    );

}


// ===================================================
// STOP RECORDING
// ===================================================

async function stopRecording()
{

    await fetch(
        "/stop"
    );

}


// ===================================================
// SAVE CONDITION PROFILE
// ===================================================

async function saveCondition()
{
    await fetch("/saveCondition");
    alert("Saved profile: " + document.getElementById("condition").innerText);
}

// ===================================================
// TEST CURRENT SIGNAL
// ===================================================

async function testSignal()
{
    const response = await fetch("/test");
    const result = await response.text();
    alert("EPDI TEST RESULT: " + result);
    updateData();
}

// ===================================================
// VIRTUAL RESET
// ===================================================

async function virtualReset()
{
    await fetch("/reset");
    updateData();
}

// ===================================================
// CLEAR ALL PROFILES
// ===================================================

async function clearAll()
{
    if(!confirm("Clear all stored condition profiles and baseline?"))
        return;

    await fetch("/clearAll");
    updateData();
}

// ===================================================
// UPDATE EVERY SECOND
// ===================================================

setInterval(
    updateData,
    1000
);


updateData();


