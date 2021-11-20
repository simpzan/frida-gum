module.exports = {
    device: "94AAY0LLTM",
    sysroot: "/home/simpzan/blueline/aosp_9_r12/out/target/product/generic_arm64/symbols/",
    processes: {
        "system_server": [
            // { name: "screenrecord", src: "frameworks/av/cmds/screenrecord", function: "", imported: [
            //     "libstagefright", "libmedia_omx", "libgui", "libEGL", "libGLESv2" ] },

            // { name: "libEGL.so", src: "", function: "egl" },
            // { name: "libinputflinger.so", src: "frameworks/native/services/inputflinger/", function: "" },

            { name: 'com.android.server.input.InputManagerService!*inject*', act: "+", type: "java" },
        ],
        "com.android.systemui": [
            { name: 'com.android.systemui.statusbar.phone.StatusBar!*', act: "+", type: "java" },
        ]
        // "com.example.myapplication": [
        //     { name: 'com.example.myapplication.*!*', act: "+", type: "java" },
        // ]
    }
};
