import com.android.build.api.dsl.LibraryExtension
import org.gradle.api.publish.maven.MavenPublication
import org.gradle.api.tasks.Sync

plugins {
    id("com.android.library")
    kotlin("android")
    `maven-publish`
}

val opensslVersion = "3.5.7"
val androidNdkVersion = "29.0.14206865"
val engineRepository = rootProject.layout.projectDirectory.dir("../..")
val opensslOutput = rootProject.layout.projectDirectory.dir(".deps/openssl-$opensslVersion")
val generatedLicenseResources = layout.buildDirectory.dir("generated/nuvioLicenseResources")
val prepareAndroidOpenSsl = rootProject.tasks.register<Exec>("prepareAndroidOpenSsl") {
    group = "build setup"
    description = "Builds pinned static OpenSSL libraries for every Android ABI"
    inputs.file(engineRepository.file("scripts/build-android-openssl.sh"))
    inputs.property("opensslVersion", opensslVersion)
    outputs.dirs(
        listOf("armeabi-v7a", "arm64-v8a", "x86", "x86_64").map { abi ->
            opensslOutput.dir("install/$abi")
        },
    )
    doFirst {
        val sdkRoot = providers.environmentVariable("ANDROID_HOME")
            .orElse(providers.environmentVariable("ANDROID_SDK_ROOT"))
            .orNull
            ?: error("ANDROID_HOME or ANDROID_SDK_ROOT must locate the Android SDK")
        commandLine(
            engineRepository.file("scripts/build-android-openssl.sh").asFile.absolutePath,
            file("$sdkRoot/ndk/$androidNdkVersion").absolutePath,
            opensslOutput.asFile.absolutePath,
        )
    }
}
val prepareAndroidLicenseResources = tasks.register<Sync>("prepareAndroidLicenseResources") {
    dependsOn(prepareAndroidOpenSsl)
    from(engineRepository.file("LICENSE")) {
        into("META-INF")
        rename { "NUVIO_ENGINE_LICENSE.txt" }
    }
    from(engineRepository.file("THIRD_PARTY_NOTICES.md")) {
        into("META-INF")
        rename { "NUVIO_ENGINE_THIRD_PARTY_NOTICES.md" }
    }
    from(opensslOutput.file("src/openssl-$opensslVersion/LICENSE.txt")) {
        into("META-INF")
        rename { "OPENSSL_LICENSE.txt" }
    }
    from(opensslOutput.file("downloads/libtorrent-LICENSE.txt")) {
        into("META-INF")
        rename { "LIBTORRENT_LICENSE.txt" }
    }
    from(opensslOutput.file("downloads/try_signal-LICENSE.txt")) {
        into("META-INF")
        rename { "TRY_SIGNAL_LICENSE.txt" }
    }
    from(opensslOutput.file("downloads/boost-LICENSE_1_0.txt")) {
        into("META-INF")
        rename { "BOOST_LICENSE_1_0.txt" }
    }
    into(generatedLicenseResources)
}

group = "com.nuvio"
version = "0.1.5"

extensions.configure<LibraryExtension> {
    namespace = "com.nuvio.engine"
    compileSdk = 36
    ndkVersion = androidNdkVersion

    defaultConfig {
        minSdk = 24
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        consumerProguardFiles("consumer-rules.pro")
        ndk {
            abiFilters += setOf("armeabi-v7a", "arm64-v8a", "x86", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DANDROID_STL=c++_static",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                    "-DNUVIO_ENGINE_ENABLE_LIBTORRENT=ON",
                    "-DNUVIO_ENGINE_BUILD_SHARED=ON",
                    "-DNUVIO_ENGINE_BUILD_TESTS=OFF",
                )
                targets += "nuvio_engine"
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "4.1.2"
        }
    }

    buildTypes {
        debug {
            isJniDebuggable = true
        }
        release {
            isMinifyEnabled = false
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    publishing {
        singleVariant("release") {
            withSourcesJar()
        }
    }

    testOptions {
        targetSdk = 36
        unitTests.isReturnDefaultValues = true
    }

    sourceSets.getByName("main").resources.srcDir(generatedLicenseResources)

    lint {
        abortOnError = true
        warningsAsErrors = true
        // These pins match the oldest consumer (NuvioTV). Dependency upgrades
        // are coordinated across both apps instead of inferred by online lint.
        disable += setOf(
            "AndroidGradlePluginVersion",
            "GradleDependency",
            "NewerVersionAvailable",
        )
    }
}

tasks.configureEach {
    if (name.startsWith("configureCMake")) {
        dependsOn(prepareAndroidOpenSsl)
    }
    if (name.startsWith("process") && name.endsWith("JavaRes")) {
        dependsOn(prepareAndroidLicenseResources)
    }
}

dependencies {
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.8.1")
    testImplementation(kotlin("test"))
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.8.1")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test:runner:1.6.2")
}

publishing {
    publications {
        register<MavenPublication>("release") {
            artifactId = "nuvio-engine-android"
            afterEvaluate {
                from(components["release"])
            }
            pom {
                name.set("Nuvio Engine for Android")
                description.set("Coroutine and Flow wrapper around the Nuvio native streaming engine")
            }
        }
    }
    repositories {
        maven {
            name = "localNuvio"
            url = uri(layout.buildDirectory.dir("repository"))
        }
    }
}
