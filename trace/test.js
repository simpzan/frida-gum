const pid = Process.id;
function log(level, ...args) {
    let e = new Error();
    let frame = e.stack.split("\n")[2].trim().substr(3); // change to 3 for grandparent func
    const time = Date.now() / 1000;
    const srcline = `${time.toFixed(3)} ${level} ${pid} ${frame}\t`;
    const text = args.map(a => JSON.stringify(a)).join(', ');
    console.log(srcline, text);
}
const noop = () => {};
log.e = log.bind(null, "E");
log.w = log.bind(null, "W");
log.i = log.bind(null, "I");
log.d = log.bind(null, "D");
log.v = log.bind(null, "V");
// log.d = noop;
log.v = noop;
global.log = log;

function getNativeFunction(module, name, argumentTypes = [], returnType = 'void') {
    const ptr = module.getExportByName(name);
    return new NativeFunction(ptr, returnType, argumentTypes);
}

function isAndroid() {
    const fnAddr = Module.getExportByName('libtrace.so', 'isAndroid');
    const isAndroid_ = new NativeFunction(fnAddr, 'int', []);
    return isAndroid_() == 1;
}

function getBuildId(soPath) {
    const getBuidId_addr = Module.getExportByName('libtrace.so', 'getBuildId');
    const getBuildId = new NativeFunction(getBuidId_addr, 'int', ['pointer', 'pointer', 'int']);
    const soPath2 = Memory.allocUtf8String(soPath);
    const length = 64;
    const bytes = Memory.alloc(length);
    const size = getBuildId(soPath2, bytes, length);
    if (0 < size && size < length) return bytes.readUtf8String();
    log.e(`failed to getBuildId(${soPath}): ${size}`);
    return "";
}
let gettid = null;
let recordTraceEvent = null;
let setBaseTimestamp = null;
function loadLibTrace(path, callback) {
    const module = Module.load(path);
    const sendDataFn = module.getExportByName('_sendDataFn');
    const sendData = new NativeCallback(callback, 'void', ['pointer', 'int', 'int']);
    log.d(`sendData ${sendData}`);
    sendDataFn.writePointer(sendData);
    const flushAll = getNativeFunction(module, 'flushAll');
    gettid = getNativeFunction(module, 'getThreadId', [], 'uint64');
    recordTraceEvent = getNativeFunction(module, 'recordTraceEvent', ['uint16', 'int32']);
    setBaseTimestamp = getNativeFunction(module, 'setBaseTimestamp', ['int64']);

    const attachCallbacks = {
        onEnter: module.getExportByName('onEnter'),
        onLeave: module.getExportByName('onLeave'),
        sendData,
        flushAll
    };
    return attachCallbacks;
}
const onTraceEvent = function(bytes, length, tid) {
    // const fn = bytes.readU64();
    // bytes = bytes.add(8);
    // const ts = bytes.readS64();
    const data = bytes.readByteArray(length);
    log.d(`event from tid ${tid}, ${length} bytes`);
    send({ type:'events', tid, pid }, data);
}

class Tracer {
    constructor() {
        const libtracePath = 'libtrace.so';
        this.attachCallbacks = loadLibTrace(libtracePath, onTraceEvent);
    }
    traceFunctions(functions, baseId = 0) {
        functions.forEach((fn, index) => {
            try {
                const addr = new NativePointer(fn.addr);
                const functionId = new NativePointer(index + baseId);
                Interceptor.attach(addr, this.attachCallbacks, functionId);
            } catch (error) {
                log.e('attach failed', error.toString(), JSON.stringify(fn));
            }
        });
        log.i(`tracing ${functions.length} function addresses.`);
    }
    exit() {
        Interceptor.detachAll();
        this.attachCallbacks.flushAll();
        this.attachCallbacks = null;
        //todo: flush events from native side.
    }
}
const tracer = new Tracer();


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

function traceJavaMethod(overloads, methodId) {
    const methods = [];
    const includeArgs = overloads.length > 1;
    for (const method of overloads) {
        const id = methodId++;
        const info = getMethodInfo(id, method, includeArgs);
        methods.push(info);
        log.i("tracing", info);
        method.implementation = function() {
            recordTraceEvent(id, 1);
            const ret = method.apply(this, arguments);
            recordTraceEvent(id, -1);
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
            for (const [ method ] of methods) {
                try {
                    const overloads = klass[method].overloads;
                    const infos = traceJavaMethod(overloads, ret.length);
                    mergeArray(ret, infos);
                } catch (err) {
                    log.e(err.toString(), method, className);
                }
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
function traceJava(specs) {
    log.i('specs', specs);
    let javaMethods = [];
    Java.perform(() => {
        const methods2Trace = new Map(); // structure: loader.class.method.id
        for (const spec of specs) if (spec.type === 'java') updateGroups(methods2Trace, spec);
        javaMethods = traceJavaMethods(methods2Trace);
        log.i('java functions', javaMethods.length);
    });
    return javaMethods;
}

rpc.exports = {
    getModuleByName(libName) {
        Module.load(libName);
        const module = Process.getModuleByName(libName);
        module.buildId = getBuildId(module.path);
        module.isAndroid = isAndroid();
        return module;
    },
    getImportedFunctions(libName) {
        return Module.enumerateImportsSync(libName).filter(fn => fn.type === 'function');
    },
    getFunctionsOfModule(libName) {
        const module = Process.getModuleByName(libName);
        let functions = module.enumerateExports();
        functions = functions.filter(s => s.type === 'function');
        log.i(`${libName} ${functions.length} exported functions`);
        return functions;
    },
    startTracing(functions, specs) {
        log.i(`startTracing`)
        const javaMethods = traceJava(specs);
        const javaCount = javaMethods.length, nativeCount = functions.length;
        const total = javaCount + nativeCount;
        if (total > Math.pow(2, 16)) {
            const stats = `total ${total}, java ${javaCount}, native ${nativeCount}`;
            throw new Error(`too many functions for uint16_t, ${stats}`);
        }
        tracer.traceFunctions(functions, javaCount);
        return javaMethods;
    },
    stopTracing() {
        tracer.exit();
    },
    getOrSetBaseTimestamp(ts) {
        if (!ts) ts = Date.now() * 1000;
        setBaseTimestamp(ts);
        return ts;
    }
};