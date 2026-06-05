const path = require("path");
const { app, BrowserWindow } = require("electron");
const { DeviceService } = require("./device-service");

const service = new DeviceService();

function createWindow() {
  const win = new BrowserWindow({
    width: 1180,
    height: 740,
    minWidth: 980,
    minHeight: 620,
    backgroundColor: "#14161f",
    title: "植物记录仪上位机",
    webPreferences: {
      preload: path.join(__dirname, "preload.js"),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false,
    },
  });

  win.loadFile(path.join(__dirname, "../renderer/index.html"));
}

app.whenReady().then(() => {
  service.register();
  createWindow();
  app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
  });
});

app.on("window-all-closed", () => {
  service.disconnect().finally(() => {
    if (process.platform !== "darwin") app.quit();
  });
});
