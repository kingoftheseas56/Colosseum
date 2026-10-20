import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    kotlin("jvm") version "2.3.0"
    `java-library`
    `maven-publish`
}

group = "com.nuvio"
version = "0.1.5"

kotlin {
    jvmToolchain(17)
    compilerOptions {
        jvmTarget.set(JvmTarget.JVM_11)
    }
}

java {
    sourceCompatibility = JavaVersion.VERSION_11
    targetCompatibility = JavaVersion.VERSION_11
    withSourcesJar()
}

dependencies {
    api("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.8.1")
    implementation("net.java.dev.jna:jna:5.19.1")
    testImplementation(kotlin("test-junit5"))
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.8.1")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher:1.10.2")
}

tasks.jar {
    archiveBaseName.set("nuvio-engine-jvm")
    isPreserveFileTimestamps = false
    isReproducibleFileOrder = true
    manifest {
        attributes["Implementation-Title"] = "Nuvio Engine JVM"
        attributes["Implementation-Version"] = project.version
    }
}

tasks.test {
    useJUnitPlatform()
    systemProperty(
        "nuvio.engine.testLibrary",
        providers.gradleProperty("nuvio.engine.testLibrary").orNull.orEmpty(),
    )
}

publishing {
    publications {
        register<MavenPublication>("jvm") {
            artifactId = "nuvio-engine-jvm"
            from(components["java"])
            pom {
                name.set("Nuvio Engine for JVM")
                description.set("Coroutine and Flow wrapper around the Nuvio Engine stable C ABI")
            }
        }
    }
}
