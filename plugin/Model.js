// Backend output is data, never markup or executable commands.
function numeric(value, min, max) {
    return typeof value === "number" && isFinite(value) && value >= min && value <= max;
}
function label(value, limit) {
    return typeof value === "string" ? value.replace(/[\x00-\x1f\x7f]/g, "").slice(0, limit) : "";
}
function json(text) {
    if (typeof text !== "string" || text.length > 65536) return null;
    try { var v = JSON.parse(text); return v && typeof v === "object" && !Array.isArray(v) ? v : null; }
    catch (_) { return null; }
}
function battery(value) {
    if (!value || !numeric(value.charge, -1, 100) || !numeric(value.watts, -1, 10000)) return null;
    return { charge: value.charge, watts: value.watts, health: numeric(value.health, -1, 200) ? value.health : -1,
        cycles: numeric(value.cycles, -1, 100000) ? value.cycles : -1,
        hours: numeric(value.hours, -1, 10000) ? value.hours : -1,
        state: ["Charging", "Discharging", "Full", "Not charging", "Unknown"].indexOf(value.state) >= 0 ? value.state : "Unknown" };
}
function status(text) { var v = json(text); return v ? battery(v) : null; }
function report(text, now) {
    var v = json(text);
    if (!v || v.version !== 1 || !battery(v.battery) || !numeric(v.generatedAt, now - 120, now + 300)) return null;
    if (!Array.isArray(v.points) || v.points.length > 120 || !Array.isArray(v.sessions) || v.sessions.length > 5) return null;
    var points = [], last = 0;
    for (var p of v.points) {
        if (!numeric(p.ts, 1, now + 300) || p.ts <= last || !numeric(p.watts, -1, 10000)) return null;
        last = p.ts; points.push({ts:p.ts, watts:p.watts, charge:numeric(p.charge,0,100) ? p.charge : -1});
    }
    var c = v.context || {}, a = v.analysis || {}, apps = [];
    if (Array.isArray(c.apps)) for (var app of c.apps.slice(0,5)) {
        if (numeric(app.cpu,0,100)) apps.push({name:label(app.name,64), cpu:app.cpu});
    }
    var context = {ts:numeric(c.ts,1,now+300) ? c.ts : 0, cpu:numeric(c.cpu,0,100) ? c.cpu : -1,
        brightness:numeric(c.brightness,0,100) ? c.brightness : -1, profile:label(c.profile,64), apps:apps};
    var analysis = { status:["ready","warming","no_matches","stale"].indexOf(a.status)>=0 ? a.status : "warming",
        matches:numeric(a.matches,0,100000) ? a.matches : 0,
        baselineWatts:numeric(a.baselineWatts,0,10000) ? a.baselineWatts : -1,
        deltaWatts:numeric(a.deltaWatts,-10000,10000) ? a.deltaWatts : 0,
        current:a.current && numeric(a.current.watts,0,10000) && numeric(a.current.end,1,now+300) ? {watts:a.current.watts,end:a.current.end} : {}, profiles:[] };
    if (analysis.status === "ready" && (analysis.matches < 3 || analysis.baselineWatts < 0 || analysis.current.watts === undefined || !numeric(a.deltaWatts,-10000,10000))) return null;
    for (var key of ["cpuCorrelation", "brightnessCorrelation"]) {
        var r = a[key] || {};
        analysis[key] = {available:r.available === true && numeric(r.r,-1,1) && numeric(r.count,12,100000), r:r.r || 0, count:r.count || 0};
    }
    if (Array.isArray(a.profiles)) for (var group of a.profiles.slice(0,5)) {
        if(numeric(group.watts,0,10000) && numeric(group.windows,3,100000))
            analysis.profiles.push({profile:label(group.profile,64),watts:group.watts,windows:group.windows});
    }
    var sessions=[];
    for(var s of v.sessions) {
        if(!numeric(s.start,1,now+300) || !numeric(s.hours,0,10000) || !numeric(s.watts,0,10000) || !numeric(s.energy,0,100000)) return null;
        sessions.push({start:s.start,hours:s.hours,watts:s.watts,energy:s.energy});
    }
    var stale = v.stale === true || !context.ts || now-context.ts>90;
    if (stale) analysis.status = "stale";
    return {battery:battery(v.battery),context:context,analysis:analysis,points:points,sessions:sessions,stale:stale};
}
if (typeof module !== "undefined") module.exports = {status:status,report:report,label:label};
