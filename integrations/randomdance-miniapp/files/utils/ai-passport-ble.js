const SERVICE = "d6b7513e-5876-48a8-a9c5-82e89f59aa00"
const CONTROL = "d6b7513e-5876-48a8-a9c5-82e89f59aa01"
const STATUS = "d6b7513e-5876-48a8-a9c5-82e89f59aa02"

function call(method, args = {}) {
  return new Promise((resolve, reject) => wx[method]({ ...args, success: resolve,
    fail: error => reject(new Error((error && error.errMsg) || `${method} 失败`)) }))
}

function utf8(value) {
  const encoded = encodeURIComponent(String(value || ""))
  const bytes = []
  for (let index = 0; index < encoded.length; index += 1) {
    if (encoded[index] === "%") { bytes.push(parseInt(encoded.slice(index + 1, index + 3), 16)); index += 2 }
    else bytes.push(encoded.charCodeAt(index))
  }
  return bytes
}

function hex(buffer) {
  return Array.from(new Uint8Array(buffer)).map(byte => byte.toString(16).padStart(2, "0")).join("")
}

class PassportBLE {
  constructor() { this.deviceId = ""; this.passportId = ""; this.scanning = false; this.adapter = false }

  async scan(onDevices) {
    if (!this.adapter) { await call("openBluetoothAdapter"); this.adapter = true }
    const found = new Map()
    this.foundHandler = result => {
      for (const device of result.devices || []) {
        const name = String(device.name || device.localName || "")
        if (/^RDP-[0-9a-f]{8}$/.test(name) && device.deviceId) {
          found.set(device.deviceId, { deviceId: device.deviceId, name })
        }
      }
      onDevices(Array.from(found.values()))
    }
    wx.onBluetoothDeviceFound(this.foundHandler)
    await call("startBluetoothDevicesDiscovery", { allowDuplicatesKey: false })
    this.scanning = true
    this.foundHandler(await call("getBluetoothDevices"))
  }

  async connect(deviceId) {
    if (this.scanning) { await call("stopBluetoothDevicesDiscovery"); this.scanning = false }
    await call("createBLEConnection", { deviceId, timeout: 10000 })
    this.deviceId = deviceId
    const services = await call("getBLEDeviceServices", { deviceId })
    if (!(services.services || []).some(item => item.uuid.toLowerCase() === SERVICE)) {
      throw new Error("所选设备不是 AI 通行证")
    }
    const characteristics = await call("getBLEDeviceCharacteristics", { deviceId, serviceId: SERVICE })
    const ids = (characteristics.characteristics || []).map(item => item.uuid.toLowerCase())
    if (!ids.includes(CONTROL) || !ids.includes(STATUS)) throw new Error("卡片蓝牙协议不匹配")
    this.passportId = await this.readId()
    return this.passportId
  }

  readId() {
    return new Promise((resolve, reject) => {
      const deviceId = this.deviceId
      let finished = false
      const done = (error, value) => {
        if (finished) return
        finished = true
        clearTimeout(timer)
        wx.offBLECharacteristicValueChange(handler)
        error ? reject(error) : resolve(value)
      }
      const handler = event => {
        if (event.deviceId !== deviceId || String(event.characteristicId).toLowerCase() !== STATUS) return
        const value = hex(event.value)
        done(value.length === 32 ? null : new Error("设备标识无效"), value)
      }
      const timer = setTimeout(() => done(new Error("读取卡片标识超时，请确认屏幕上的配对码")), 15000)
      wx.onBLECharacteristicValueChange(handler)
      call("readBLECharacteristicValue", { deviceId, serviceId: SERVICE, characteristicId: STATUS })
        .catch(error => done(error))
    })
  }

  async write(frame) {
    if (!this.deviceId || !this.passportId) throw new Error("请先连接 AI 通行证")
    await call("writeBLECharacteristicValue", { deviceId: this.deviceId, serviceId: SERVICE,
      characteristicId: CONTROL, value: Uint8Array.from(frame).buffer, writeType: "write" })
  }

  async provision(ssid, password) {
    const name = utf8(ssid), secret = utf8(password)
    if (!name.length || name.length > 32 || secret.length > 64) throw new Error("Wi-Fi 名称或密码长度无效")
    await this.write([1, name.length, secret.length])
    for (let i = 0; i < name.length; i += 19) await this.write([2, ...name.slice(i, i + 19)])
    for (let i = 0; i < secret.length; i += 19) await this.write([3, ...secret.slice(i, i + 19)])
    await this.write([4])
  }

  refreshTheme(expectedId) {
    if (!this.passportId || this.passportId !== expectedId) throw new Error("连接的卡片与账号设备不一致")
    return this.write([5])
  }

  async close() {
    if (this.foundHandler) { wx.offBluetoothDeviceFound(this.foundHandler); this.foundHandler = null }
    if (this.scanning) { try { await call("stopBluetoothDevicesDiscovery") } catch (_) {} this.scanning = false }
    if (this.deviceId) { try { await call("closeBLEConnection", { deviceId: this.deviceId }) } catch (_) {} }
    this.deviceId = this.passportId = ""
    if (this.adapter) { try { await call("closeBluetoothAdapter") } catch (_) {} this.adapter = false }
  }
}

module.exports = { PassportBLE, SERVICE, CONTROL, STATUS, utf8, hex }
