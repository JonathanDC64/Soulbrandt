# Copies FFmpeg's DLLs from FROM (a runtime build's ffmpeg-win64/bin) to TO (an exe's folder), when FROM has any.
# Run after each link of des_boot and des_host (des_copy_ffmpeg_dlls in CMakeLists.txt). file(COPY) skips a DLL
# that is already there with the same time stamp.
# USB_FROM (a runtime build's libusb-win64, ps3recomp's cmake/libusb_dep.cmake) gives libusb-1.0.dll, which SDL3
# opens for the Switch 2 controllers over USB.
file(GLOB _dlls "${FROM}/avcodec-*.dll" "${FROM}/avutil-*.dll")
if(USB_FROM AND EXISTS "${USB_FROM}/libusb-1.0.dll")
    list(APPEND _dlls "${USB_FROM}/libusb-1.0.dll")
endif()
if(_dlls)
    file(COPY ${_dlls} DESTINATION "${TO}")
endif()
