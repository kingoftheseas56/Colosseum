// swift-tools-version: 6.0

import PackageDescription

let package = Package(
    name: "NuvioEngine",
    platforms: [
        .macOS(.v11),
        .iOS("16.1"),
    ],
    products: [
        .library(name: "NuvioEngine", targets: ["NuvioEngine"]),
    ],
    targets: [
        .binaryTarget(
            name: "CNuvioEngine",
            path: "NuvioEngine.xcframework"
        ),
        .target(
            name: "NuvioEngine",
            dependencies: ["CNuvioEngine"],
            linkerSettings: [
                .linkedLibrary("c++"),
                .linkedFramework("Security"),
                .linkedFramework("SystemConfiguration"),
                .linkedFramework("CoreFoundation"),
            ]
        ),
        .testTarget(
            name: "NuvioEngineTests",
            dependencies: ["NuvioEngine"]
        ),
    ]
)
