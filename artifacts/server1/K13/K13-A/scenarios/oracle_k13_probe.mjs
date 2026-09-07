// This probe replays only the JavaScript settings expressions exercised by K13-A.
// It is not a second implementation of the server and is not a production dependency.
// Oracle: C:\b\Colosseum-Server-1.0-Planning-Pack\oracle\stremio-service-v4.21.1-server-bundle\server.js
// M106 12792-12833; M194 19675-19682; M400 34767-34775;
// M414 35959-36170; M564 46578-46979.

const isPositiveInteger = value => typeof value === "number" && Number.isFinite(value)
  && value > 0 && Math.trunc(value) === value;

function makeSettings({ appPath, android = false, disableCaching = false, loaded = {} }) {
  const self = {
    get serverVersion() {
      return "4.21.1";
    },
    set serverVersion(_) {},
  };

  self.appPath = appPath;
  self.cacheRoot = Object.prototype.hasOwnProperty.call(self, "cacheRoot") ? self.cacheRoot : appPath;
  self.cacheSize = disableCaching ? 0 : Object.prototype.hasOwnProperty.call(self, "cacheSize")
    ? self.cacheSize : android ? 0 : 2147483648;
  self.btMaxConnections = isPositiveInteger(self.btMaxConnections) ? self.btMaxConnections : 55;
  self.btHandshakeTimeout = isPositiveInteger(self.btHandshakeTimeout) ? self.btHandshakeTimeout : 20000;
  self.btRequestTimeout = isPositiveInteger(self.btRequestTimeout || self.btConnectionTimeout)
    ? self.btRequestTimeout : 4000;
  self.btDownloadSpeedSoftLimit = isPositiveInteger(self.btDownloadSpeedSoftLimit)
    ? self.btDownloadSpeedSoftLimit : 2621440;
  self.btDownloadSpeedHardLimit = isPositiveInteger(self.btDownloadSpeedHardLimit)
    ? self.btDownloadSpeedHardLimit : 3670016;
  self.btMinPeersForStable = isPositiveInteger(self.btMinPeersForStable)
    ? self.btMinPeersForStable : 5;
  self.remoteHttps = typeof self.remoteHttps === "string" ? self.remoteHttps : "";
  self.localAddonEnabled = Object.prototype.hasOwnProperty.call(self, "localAddonEnabled")
    && self.localAddonEnabled;
  self.transcodeHorsepower = self.transcodeHorsepower ? self.transcodeHorsepower : .75;
  self.transcodeMaxBitRate = isPositiveInteger(self.transcodeMaxBitRate)
    ? self.transcodeMaxBitRate : 0;
  self.transcodeConcurrency = isPositiveInteger(self.transcodeConcurrency)
    ? self.transcodeConcurrency : 1;
  self.transcodeTrackConcurrency = isPositiveInteger(self.transcodeTrackConcurrency)
    ? self.transcodeTrackConcurrency : 1;
  self.transcodeHardwareAccel = !Object.prototype.hasOwnProperty.call(self, "transcodeHardwareAccel")
    || self.transcodeHardwareAccel;
  self.transcodeProfile = Object.prototype.hasOwnProperty.call(self, "transcodeProfile")
    ? self.transcodeProfile : null;
  self.allTranscodeProfiles = [];
  self.transcodeMaxWidth = Object.prototype.hasOwnProperty.call(self, "transcodeMaxWidth")
    ? self.transcodeMaxWidth : 1920;
  self.proxyStreamsEnabled = Object.prototype.hasOwnProperty.call(self, "proxyStreamsEnabled")
    && self.proxyStreamsEnabled;

  Object.assign(self, loaded);
  return self;
}

const defaults = makeSettings({ appPath: "TRACE_APP" });
console.log(`K13-01 default.cacheSize=${defaults.cacheSize}`);
console.log(`K13-01 default.btMaxConnections=${defaults.btMaxConnections}`);
console.log(`K13-01 default.remoteHttps=${defaults.remoteHttps}`);
console.log(`K13-01 default.transcodeProfile=${JSON.stringify(defaults.transcodeProfile)}`);

const loaded = makeSettings({
  appPath: "TRACE_APP",
  loaded: {
    serverVersion: "9.9.9",
    cacheSize: 0,
    btMaxConnections: 99,
    unknownKey: "retained",
  },
});
console.log(`K13-01 load.cacheSize=${loaded.cacheSize}`);
console.log(`K13-01 load.btMaxConnections=${loaded.btMaxConnections}`);
console.log(`K13-01 load.unknownKey=${loaded.unknownKey}`);
console.log(`K13-01 load.serverVersion=${loaded.serverVersion}`);

Object.assign(loaded, { unknownKey: "overridden", newUnknown: true });
console.log(`K13-03 persisted.unknownKey=${loaded.unknownKey}`);
console.log(`K13-03 persisted.newUnknown=${loaded.newUnknown}`);
console.log(`K13-03 infinite-null=${JSON.stringify({ cacheSize: Infinity }) === "{\"cacheSize\":null}" ? 1 : 0}`);
