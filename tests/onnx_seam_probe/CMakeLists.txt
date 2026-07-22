# tests/onnx_seam_probe/CMakeLists.txt
# Configure-only proof that the imported ONNX target resolves. Not part of the
# app build; run manually after staging the runtime.
cmake_minimum_required(VERSION 3.16)
project(onnx_seam_probe LANGUAGES CXX)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../native/cmake/OnnxRuntime.cmake)
get_target_property(_onnx_dll onnxruntime::onnxruntime IMPORTED_LOCATION)
message(STATUS "ONNX_SEAM_OK ${_onnx_dll}")
