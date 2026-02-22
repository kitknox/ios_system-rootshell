// swift-tools-version:5.3
import PackageDescription

_ = Package(
    name: "xcfs",
    platforms: [.macOS("11")],
    dependencies: [
        .package(url: "https://github.com/holzschu/FMake", from: "0.0.19")
    ],
    
    targets: [
        .binaryTarget(
            name: "openssl",
            path: "../../openssl_ios/.build/libssl.xcframework"
        ),
        .binaryTarget(
            name: "libcrypto",
            path: "../../openssl_ios/.build/libcrypto.xcframework"
        ),
        .target(
            name: "build",
            dependencies: ["FMake"]
        ),
    ]
)
