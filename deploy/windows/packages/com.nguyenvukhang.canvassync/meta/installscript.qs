function Component()
{
}

Component.prototype.createOperations = function()
{
    if (systemInfo.productType === "windows") {
        // Let Qt Installer Framework stop the app before replacing its files.
        component.addStopProcessForUpdateRequest("Canvas Sync.exe");
    }

    component.createOperations();

    if (systemInfo.productType === "windows") {
        component.addOperation("CreateShortcut",
                "@TargetDir@/Canvas Sync.exe",
                "@StartMenuDir@/Canvas Sync.lnk",
                "iconPath=@TargetDir@/Canvas Sync.exe");
    }
}
