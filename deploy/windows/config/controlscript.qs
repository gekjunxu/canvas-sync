function Controller()
{
}

var updatingExistingInstallation = false;

function isExistingInstallation()
{
    try {
        var targetDir = installer.value("TargetDir");
        if (!targetDir) {
            return false;
        }
        var exePath = targetDir + "/Canvas Sync.exe";
        var toolPath = targetDir + "/maintenancetool.exe";
        return systemInfo.productType === "windows" &&
            installer.fileExists(exePath) && installer.fileExists(toolPath);
    } catch (e) {
        return false;
    }
}

Controller.prototype.IntroductionPageCallback = function()
{
    try {
        var page = gui.currentPageWidget();
        if (!page) {
            return;
        }

        updatingExistingInstallation = isExistingInstallation();
        if (updatingExistingInstallation) {
            page.title = "Update Canvas Sync";
            if (page.MessageLabel) {
                var ver = installer.value("ProductVersion", "the latest version");
                page.MessageLabel.setText(
                    "An existing installation of Canvas Sync was detected.\n" +
                    "Setup will update Canvas Sync to version " + ver + ". Your settings will be kept."
                );
            }
        }
    } catch (e) {
        console.log("IntroductionPageCallback error: " + e);
    }
}

Controller.prototype.ReadyForInstallationPageCallback = function()
{
    try {
        var page = gui.currentPageWidget();
        updatingExistingInstallation = isExistingInstallation();
        if (page && updatingExistingInstallation) {
            page.title = "Ready to Update";
        }
    } catch (e) {
        console.log("ReadyForInstallationPageCallback error: " + e);
    }
}

Controller.prototype.FinishedPageCallback = function()
{
    try {
        var page = gui.currentPageWidget();
        if (!page) {
            return;
        }

        if (updatingExistingInstallation) {
            page.title = "Update Complete";
            if (page.MessageLabel) {
                var ver = installer.value("ProductVersion", "the latest version");
                page.MessageLabel.setText(
                    "Canvas Sync has been successfully updated to version " + ver + "."
                );
            }
        }
    } catch (e) {
        console.log("FinishedPageCallback error: " + e);
    }
}
