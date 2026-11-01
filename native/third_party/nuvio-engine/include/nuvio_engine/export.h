#ifndef NUVIO_ENGINE_EXPORT_H
#define NUVIO_ENGINE_EXPORT_H

#if defined(_WIN32)
#if defined(NUVIO_ENGINE_BUILDING_SHARED)
#define NUVIO_ENGINE_API __declspec(dllexport)
#elif defined(NUVIO_ENGINE_USING_SHARED)
#define NUVIO_ENGINE_API __declspec(dllimport)
#else
#define NUVIO_ENGINE_API
#endif
#elif defined(NUVIO_ENGINE_BUILDING_SHARED)
#define NUVIO_ENGINE_API __attribute__((visibility("default")))
#else
#define NUVIO_ENGINE_API
#endif

#if defined(_WIN32)
#define NUVIO_ENGINE_CPP_API
#else
#define NUVIO_ENGINE_CPP_API NUVIO_ENGINE_API
#endif

#endif
