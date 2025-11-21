// swift-tools-version:5.3
import PackageDescription

_ = Package(
    name: "xcfs",
    platforms: [.macOS("11")],
    dependencies: [
        .package(url: "https://github.com/holzschu/FMake", from: "0.0.19")
    ],
    
    targets: [
        // ssh2:
        .binaryTarget(
            name: "libssh2",
            path: "../../libssh2-for-iOS/libssh2.xcframework"
        ),
        .binaryTarget(
            name: "openssl",
            path: "../../libssh2-for-iOS/libssl.xcframework"
        ),
        .binaryTarget(
            name: "libcrypto",
            path: "../../libssh2-for-iOS/libcrypto.xcframework"
        ),
        .target(
            name: "build",
            dependencies: ["FMake"]
        ),
    ]
)
