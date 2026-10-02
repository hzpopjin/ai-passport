const api = require("../../utils/api.js")
const passport = require("../../utils/ai-passport.js")
const { PassportBLE } = require("../../utils/ai-passport-ble.js")

Page({
  data: { loggedIn: false, loading: false, busy: false, devices: [], themes: [], message: "",
    nearby: [], connectedId: "", wifiSsid: "", wifiPassword: "" },

  onUnload() { if (this.ble) this.ble.close().catch(() => {}) },

  onShow() { this.refresh() },

  async refresh() {
    const loggedIn = api.isLoggedIn()
    this.setData({ loggedIn, message: "" })
    if (!loggedIn) { this.setData({ devices: [], themes: [] }); return }
    this.setData({ loading: true })
    try {
      const [devices, themes] = await Promise.all([passport.devices(), passport.themes()])
      if (!api.isLoggedIn()) return
      const cleanThemes = Array.isArray(themes) ? themes.filter(x => x && /^[a-z0-9_-]{1,64}$/.test(x.id)) : []
      this.setData({ devices: (Array.isArray(devices) ? devices : []).map(d => ({
        ...d, shortId: String(d.id || "").slice(-8),
        themeOptions: cleanThemes.map(t => t.title || t.id),
        themeIndex: Math.max(0, cleanThemes.findIndex(t => t.id === d.desired_theme))
      })), themes: cleanThemes })
    } catch (error) {
      this.setData({ message: error.message || "设备信息加载失败" })
    } finally {
      this.setData({ loading: false })
    }
  },

  openLogin() { wx.switchTab({ url: "/pages/wp-user/wp-user" }) },

  scanPairing() {
    if (this.data.busy) return
    wx.scanCode({ onlyFromCamera: true, scanType: ["qrCode"], success: async result => {
      const code = passport.pairingCode(result.result)
      if (!code) { wx.showToast({ title: "这不是 AI 通行证绑定码", icon: "none" }); return }
      const accepted = await new Promise(resolve => wx.showModal({ title: "绑定 AI 通行证",
        content: "请确认二维码显示在你手中的卡片上；卡片端还需再次确认。",
        success: res => resolve(res.confirm), fail: () => resolve(false) }))
      if (!accepted) return
      this.setData({ busy: true })
      try {
        const claim = await passport.claim(code)
        const accountId = claim && claim.account_id
        if (!Number.isInteger(accountId) || accountId <= 0) throw new Error("账号校验信息缺失")
        wx.showModal({ title: "等待卡片确认", content: `请核对卡片显示的账号编号 #${accountId}，再按确认键。`, showCancel: false })
        await this.refresh()
      } catch (error) {
        wx.showToast({ title: error.message || "绑定失败", icon: "none" })
      } finally { this.setData({ busy: false }) }
    } })
  },

  async changeTheme(event) {
    if (this.data.busy) return
    const device = this.data.devices[event.currentTarget.dataset.index]
    const theme = this.data.themes[Number(event.detail.value)]
    if (!device || !theme) return
    this.setData({ busy: true })
    try {
      await passport.setTheme(device.id, theme.id)
      wx.showToast({ title: "主题已选择，连接卡片后手动刷新", icon: "none" })
      await this.refresh()
    } catch (error) { wx.showToast({ title: error.message || "更换失败", icon: "none" }) }
    finally { this.setData({ busy: false }) }
  },

  async toggleCard(event) {
    if (this.data.busy) return
    const device = this.data.devices[event.currentTarget.dataset.index]
    if (!device) return
    this.setData({ busy: true })
    try { await passport.setCardPublic(device.id, !device.card_public); await this.refresh() }
    catch (error) { wx.showToast({ title: error.message || "设置失败", icon: "none" }) }
    finally { this.setData({ busy: false }) }
  },

  copyCard(event) {
    const device = this.data.devices[event.currentTarget.dataset.index]
    if (device && /^https:\/\/ai-passport\.randomdance\.cn\/card\/[0-9a-f]{32}$/.test(device.card_url)) {
      wx.setClipboardData({ data: device.card_url })
    }
  },

  async scanBluetooth() {
    if (this.data.busy) return
    if (this.ble) await this.ble.close()
    this.ble = new PassportBLE()
    this.setData({ nearby: [], connectedId: "", busy: true })
    try { await this.ble.scan(nearby => this.setData({ nearby })) }
    catch (error) { wx.showToast({ title: error.message || "蓝牙搜索失败", icon: "none" }) }
    finally { this.setData({ busy: false }) }
  },

  async connectBluetooth(event) {
    if (this.data.busy || !this.ble) return
    const deviceId = event.currentTarget.dataset.id
    this.setData({ busy: true })
    try {
      const id = await this.ble.connect(deviceId)
      this.setData({ connectedId: id, nearby: [] })
      wx.showToast({ title: `已连接卡片 ${id.slice(-8)}`, icon: "none" })
    } catch (error) {
      wx.showToast({ title: error.message || "连接失败", icon: "none" })
      await this.ble.close()
    } finally { this.setData({ busy: false }) }
  },

  onWifiSsid(event) { this.setData({ wifiSsid: event.detail.value }) },
  onWifiPassword(event) { this.setData({ wifiPassword: event.detail.value }) },

  async provisionWifi() {
    if (this.data.busy || !this.ble || !this.data.connectedId) return
    this.setData({ busy: true })
    try {
      await this.ble.provision(this.data.wifiSsid, this.data.wifiPassword)
      this.setData({ wifiPassword: "" })
      wx.showModal({ title: "Wi-Fi 已发送", content: "请看卡片屏幕确认联网结果。密码不会保存在小程序中。", showCancel: false })
    } catch (error) { wx.showToast({ title: error.message || "配网失败", icon: "none" }) }
    finally { this.setData({ busy: false }) }
  },

  async refreshTheme(event) {
    if (this.data.busy || !this.ble) return
    const device = this.data.devices[event.currentTarget.dataset.index]
    if (!device) return
    this.setData({ busy: true })
    try {
      await this.ble.refreshTheme(device.id)
      wx.showToast({ title: "已请求卡片下载主题", icon: "none" })
    } catch (error) { wx.showToast({ title: error.message || "刷新失败", icon: "none" }) }
    finally { this.setData({ busy: false }) }
  },

  unbind(event) {
    if (this.data.busy) return
    const device = this.data.devices[event.currentTarget.dataset.index]
    if (!device) return
    wx.showModal({ title: "解绑设备", content: "解绑后 NFC 信息卡会变为不公开，设备可绑定到其他账号。", success: async res => {
      if (!res.confirm) return
      this.setData({ busy: true })
      try { await passport.unbind(device.id); await this.refresh() }
      catch (error) { wx.showToast({ title: error.message || "解绑失败", icon: "none" }) }
      finally { this.setData({ busy: false }) }
    } })
  }
})
