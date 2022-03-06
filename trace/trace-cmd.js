const utils = require('./utils.js');
const fs = require('fs');
const readline = require('readline');
const log = console.log.bind(console);

function getFunctionMaps() {
    // utils.runCmd('trace-cmd report -tR | grep trace-cmd-8500 > ./trace.txt');
    utils.runCmd('trace-cmd report -f > ./fns.txt');
    const out = fs.readFileSync('./fns.txt', 'utf8');
    const functions = {};
    out.split('\n').forEach(line => {
        const parts = line.split(' ');
        const addr = parseInt(parts[0], 16) + 4;
        functions[addr] = parts;
    });
    return functions;
}

class Stacktrace {
    constructor(tid, tname) {
        this.tid = tid;
        this.tname = tname;
        this.stack = [];
    }
    addEvent(ts, name, begin=false) {
        // log(arguments)
        if (begin) {
            this.stack.push({ts, name, begin});
        } else if (this.stack.length) {
            const entry = this.stack.pop();
            const name = entry.name;
            const dur = ts - entry.ts;
            const ph = "X";
            ts = entry.ts;
            const pid = this.tid;
            const tid = this.tid;
            // log(this.tid, name, entry.ts, dur);
            return {name, ph, ts, dur, pid, tid};
        } else {
            log('error', arguments, this.stack.length);
        }
    }
}


async function processLineByLine(file) {
  const fileStream = fs.createReadStream(file);
  const rl = readline.createInterface({
    input: fileStream,
    crlfDelay: Infinity
  });
  const re = /\s*(.+)-(\d+) +\[\d{3}\] (.+): funcgraph_(.+): *func=0x(\w+) /
  const stacktraceByTid = {};
  const functions = getFunctionMaps();
  const ctf = new utils.ChromeTracingFile("./t.json");
  for await (const line of rl) {
    //   log(line)
    const m = re.exec(line);
    if (!m) {
        log('error', line);
        continue;
    }
    const name = m[1];
    const tid = parseInt(m[2], 10);
    const ts = parseInt(m[3], 10) / 1000;
    const phase = m[4];
    const fnAddr = parseInt(m[5], 16);
    const stacktrace = stacktraceByTid[tid] = stacktraceByTid[tid] || new Stacktrace(tid, name);
    const fn = functions[fnAddr];
    if (!fn) {
        log(phase, fn, m);
        continue;
    }
    const event = stacktrace.addEvent(ts, fn[1], phase==='entry');
    ctf.writeObject(event);
  }
  for (const stack of Object.values(stacktraceByTid)) {
    const pid = stack.tid;
    const tid = pid;
    const name = stack.tname;
    const entry = {"ts":0, "ph":"M", "name":"thread_name", pid, tid, "args":{name}};
    ctf.writeObject(entry);
  }
  ctf.close();
}

function getTidMaps() {
    const psOut = utils.runCmd('ps -AT -o pid,tid,comm');
    const commandByTid = {};
    psOut.split('\n').forEach(line => {
        const parts = line.split(' ').filter(p => p && p.length);
        const pid = parseInt(parts[0], 10);
        if (!pid) return;
        const tid = parseInt(parts[1], 10);
        const comm = parts.slice(2).join(' ');
        commandByTid[tid] = { pid, tid, comm };
    });
    log(commandByTid);
    return commandByTid;
}

async function main() {
    getTidMaps();
    const file = './trace.txt';
    await processLineByLine(file);
    utils.runCmd('zip -r t.zip t.json');
}
main();