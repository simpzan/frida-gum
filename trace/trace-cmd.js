const fs = require('fs');
const readline = require('readline');
const log = console.log.bind(console);

class ChromeTracingFile {
    constructor(filename) {
        this.sink = fs.createWriteStream(filename);
        this.sink.write("[\n");
    }
    writeObject(obj) {
        if (!obj) return;
        this.sink.write(JSON.stringify(obj));
        this.sink.write(",\n");
    }
    close() {
        this.sink.write("{}]\n");
        this.sink.end();
    }
}

function runCmd(cmd) {
    try {
        console.info(`cmd: ${cmd}`);
        const cp = require("child_process");
        const output = cp.execSync(cmd);
        const result = output.toString("utf8");
        console.info(`result:\n${result}`);
        return result.trim();
    } catch (err) {
        console.error(err);
        return null;
    }
}

function getFunctionMaps() {
    // runCmd('trace-cmd report -tR | grep trace-cmd-8500 > ./trace.txt');
    runCmd('trace-cmd report -f > ./fns.txt');
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
  const ctf = new ChromeTracingFile("./t.json");
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
  ctf.close();
}

async function main() {
    const file = './trace.txt';
    await processLineByLine(file);
    runCmd('zip -r t.zip t.json');
}
main();