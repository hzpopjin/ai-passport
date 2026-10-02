**简体中文** · [English](README.md)

# 随机舞蹈小程序 Passport 快照

本目录仅携带独立随机舞蹈小程序工作目录中的 AI Passport 集成。检查时基线提交为 `e0d27b5ca81b4f927ac546b8b11464bcb326eaad`，不包含客户端的其他变更。

`files/` 包含四个 `pages/ai-passport/ai-passport.*` 页面文件和两个 `utils/ai-passport*.js` 模块。将其内容复制到小程序根目录。`wiring.patch` 针对所检查基线增加页面路由、设置页方法和入口：

```sh
git apply --check /path/to/ai-passport/integrations/randomdance-miniapp/wiring.patch
git apply /path/to/ai-passport/integrations/randomdance-miniapp/wiring.patch
```

复制或应用前检查目标客户端自身的 AGENTS 指令与当前修改。如果页面已注册，应合并现有实现，避免重复添加。当前 API 工具需提供 `isLoggedIn`、`ensureBusinessLogin`、`unwrapBusiness`，以及支持 `auth: "business"`、`omitData` 的 `request`。所检查基线已提供这些接口，本快照不替换共享 API 工具。

页面支持正式账号扫码认领、设备列表、官方主题选择、蓝牙 Wi-Fi 配网及刷新、NFC 信息卡公开／私密控制、复制网址与解绑。凭证通过卡片的认证蓝牙协议发送，不保存在小程序资料中。

可本地检查 JavaScript 语法与 JSON。微信开发者工具编译、正式账号流程、系统蓝牙配对、实体 Wi-Fi 配网、主题下载及 NFC 行为尚未验收。本快照不上传或发布客户端。先部署[后端集成](../randomdance-backend/README.zh_CN.md)，再联调 `/passport` 调用。
