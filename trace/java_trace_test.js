function log(level, ...args) {
    let e = new Error();
    let frame = e.stack.split("\n")[2]; // change to 3 for grandparent func
    let lineNumber = frame.split(":").reverse()[1];
    let functionName = frame.split(" ")[5];
    const time = Date.now() / 1000;
    const srcline = `${time.toFixed(3)} ${level} ${pid} ${functionName}:${lineNumber}\t`;
    const text = args.map(a => JSON.stringify(a)).join(', ');
    console.log(srcline, text);
}
const noop = () => {};
const pid = Process.id;
log.e = log.bind(null, "E");
log.w = log.bind(null, "W");
log.i = log.bind(null, "I");
log.d = log.bind(null, "D");
log.v = log.bind(null, "V");
// log.d = noop;
log.v = noop;
global.log = log;


function mapGetOrCreate(map, key) {
    let value = map.get(key);
    if (value) return value;
    value = new Map();
    map.set(key, value);
    return value;
}
function mergeArray(to, from) {
    for (const e of from) to.push(e);
}
function getMethodInfo(id, method, includeArgs) {
    const ret = { id, "class": method.holder.$className, "method": method.methodName };
    if (includeArgs) ret.args = method.argumentTypes.map(a => a.className);
    return ret;
}
let javaMethods = null;
function traceJavaEvent(id, phase) {
    const ph = phase > 0 ? '<' : '>';
    const ts = Date.now();
    const method = javaMethods ? javaMethods[id] : id;
    log.i(ph, method, ts);
}

function traceJavaMethod(overloads, methodId) {
    const methods = [];
    const includeArgs = overloads.length > 1;
    for (const method of overloads) {
        const id = methodId++;
        const info = getMethodInfo(id, method, includeArgs);
        methods.push(info);
        log.i("tracing", info);
        method.implementation = function() {
            traceJavaEvent(id, 1);
            const ret = method.apply(this, arguments);
            traceJavaEvent(id, -1);
            return ret;
        }
    }
    return methods;
}
function traceJavaMethods(groups) {
    const ret = [];
    for (const [ loader, classes ] of groups) {
        const factory = Java.ClassFactory.get(loader);
        for (const [ className, methods ] of classes) {
            let klass = factory.use(className);
            for (const [ method, _ ] of methods) {
                const overloads = klass[method].overloads;
                const infos = traceJavaMethod(overloads, ret.length);
                mergeArray(ret, infos);
            }
        }
    }
    return ret;
}
function updateGroups(ret, spec) {
    const groups = Java.enumerateMethods(spec.name);
    const add = spec.act === "+";
    for (const group of groups) {
        const classes = mapGetOrCreate(ret, group.loader);
        for (const klass of group.classes) {
            const methods = mapGetOrCreate(classes, klass.name);
            for (const method of klass.methods) {
                // log.d(add, klass.name, method);
                if (add) methods.set(method, null);
                else methods.delete(method);
            }
        }
    }
}

Java.perform(() => {
    const specs = [
        { name: "android.app.Activity!*", act: "+" },
        // { name: "android.app.Activity!startActivityForResult*", act: "-" },
        { name: 'com.example.myapplication.*!*', act: "+" },
    ];
    const methods2Trace = new Map(); // structure: loader.class.method.id
    for (const spec of specs) updateGroups(methods2Trace, spec);
    const functions = traceJavaMethods(methods2Trace);
    javaMethods = functions;
    log.i('functions', functions);
});
