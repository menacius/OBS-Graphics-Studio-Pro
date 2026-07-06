#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QHash>
#include <vector>

#include "effects/layer-effects.h"
#include "bgl-plugin-api.h"

struct BglEffectExtensionDefinition {
    QString id;
    QString displayName;
    QString category;
    QString shaderPath;
    QString manifestPath;
    QString providerId;
    QString providerVersion;
    QJsonObject parameterSchema;
    QJsonObject defaults;
    QJsonObject editorSchema;
    QJsonObject presetIndex;
    QJsonObject assetIndex;
    QJsonObject capabilities;
    QJsonObject animationSchema;
    QJsonArray canvasHandles;
    QJsonObject parameterMetadata;
    QJsonObject customPropertyWidgets;
    QJsonObject requirements;
    QJsonObject stateSerialization;
    QJsonArray renderPasses;
    QJsonArray inputs;
    QJsonArray auxiliaryInputs;
    QJsonArray layerReferences;
    QString declaredColorSpace;
    QString declaredAlphaContract;
    QString backend;
    int declaredInputCount = 1;
    bool cpuWorkerOnly = false;
    bool multiPass = false;
    QString basePath;
    QString pluginPath;
    uint32_t schemaVersion = 1;
    bgl_validate_state_v2_fn validateState = nullptr;
    bgl_migrate_state_v2_fn migrateState = nullptr;
    bgl_release_string_v2_fn releaseString = nullptr;
    QStringList techniques;
    bool nativeProvider = false;
    bool builtIn = false;
    LayerEffectType builtInType = LayerEffectType::BackgroundColor;
};

class BglEffectExtensionCatalog {
public:
    static BglEffectExtensionCatalog &instance();

    void reload();
    void rescan() { reload(); }
    void shutdown();
    QStringList quarantineEntries() const;
    QStringList blacklistEntries() const;
    void clearQuarantine();
    void clearBlacklist();
    void blacklistPath(const QString &path);
    void retainPluginInstance(const QString &providerId);
    void releasePluginInstance(const QString &providerId);
    const std::vector<BglEffectExtensionDefinition> &effects() const { return effects_; }
    const BglEffectExtensionDefinition *find(const QString &id) const;
    const BglEffectExtensionDefinition *find(LayerEffectType type) const;
    static QString builtInId(LayerEffectType type);
    static bool builtInTypeForId(const QString &id, LayerEffectType *type);
    QStringList diagnostics() const { return diagnostics_; }

private:
    BglEffectExtensionCatalog() = default;
    void scanManifestRoot(const QString &root, int depth = 0);
    void loadManifest(const QString &manifestPath);
    void scanNativeRoot(const QString &root, int depth = 0);
    void quarantinePluginPath(const QString &path, const QString &reason, const QString &pluginName = QString(), const QString &pluginVersion = QString());
    bool pathIsQuarantinedOrBlacklisted(const QString &path) const;
    bool canUnloadProvider(const QString &providerId) const;

    std::vector<BglEffectExtensionDefinition> effects_;
    QStringList diagnostics_;
    QStringList quarantine_;
    QStringList blacklist_;
    QHash<QString, int> active_instances_;
};
