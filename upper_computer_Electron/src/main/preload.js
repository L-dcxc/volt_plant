const { contextBridge, ipcRenderer } = require("electron");

const api = {
  listPorts: () => ipcRenderer.invoke("ports:list"),
  connect: (opts) => ipcRenderer.invoke("device:connect", opts),
  disconnect: () => ipcRenderer.invoke("device:disconnect"),
  readInfo: () => ipcRenderer.invoke("device:info"),
  readStatus: () => ipcRenderer.invoke("device:status"),
  readClock: () => ipcRenderer.invoke("device:clock:read"),
  writeClock: (dto) => ipcRenderer.invoke("device:clock:write", dto),
  setRun: (running) => ipcRenderer.invoke("device:run", running),
  command: (name) => ipcRenderer.invoke("device:command", name),
  readChannels: (channels) => ipcRenderer.invoke("device:channels:read", channels),
  readConfig: () => ipcRenderer.invoke("device:config:read"),
  writeConfig: (payload) => ipcRenderer.invoke("device:config:write", payload),
  openDir: () => ipcRenderer.invoke("files:openDir"),
  deleteFiles: (rows) => ipcRenderer.invoke("files:delete", rows),
  downloadFiles: (rows) => ipcRenderer.invoke("files:download", rows),
  cancelDownload: () => ipcRenderer.invoke("files:cancelDownload"),
  onDownloadProgress: (callback) => {
    const listener = (_event, payload) => callback(payload);
    ipcRenderer.on("files:downloadProgress", listener);
    return () => ipcRenderer.off("files:downloadProgress", listener);
  },
};

contextBridge.exposeInMainWorld("plantApi", api);
