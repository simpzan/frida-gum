module.exports = {
    device: "94AAY0LLTM",
    sysroot: "/home/simpzan/blueline/aosp_9_r12/out/target/product/generic_arm64/symbols/",
    process: "screenrecord",
    modules: [
        { name: "screenrecord", src: "frameworks/av/cmds/screenrecord", function: "", imported: [
            "libstagefright", "libmedia_omx", "libgui", "libEGL", "libGLESv2" ] },

        // { name: "libEGL.so", src: "", function: "egl" },
        // { name: "libGLESv2.so", src: "", function: "gl" },
    ]
};
