const api = require("./api.js")

const PAIR_URL = /^https:\/\/ai-passport\.randomdance\.cn\/pair\/([0-9a-f]{32})$/

function pairingCode(result) {
  const match = PAIR_URL.exec(String(result || ""))
  return match ? match[1] : ""
}

async function request(path, method = "GET", data) {
  if (!api.isLoggedIn()) throw new Error("请先登录随机舞蹈中文站账号")
  await api.ensureBusinessLogin()
  return api.unwrapBusiness(await api.request({ path: `/passport${path}`, method,
    data, omitData: data === undefined, auth: "business" }))
}

function devices() { return request("/devices") }
function themes() { return request("/themes") }
function claim(code) {
  if (!/^[0-9a-f]{32}$/.test(code)) throw new Error("绑定二维码无效")
  return request(`/pairings/${code}/claim`, "POST", {})
}
function setTheme(id, themeId) {
  if (!/^[0-9a-f]{32}$/.test(id) || !/^[a-z0-9_-]{1,64}$/.test(themeId)) throw new Error("主题选择无效")
  return request(`/devices/${id}/theme`, "PUT", { theme_id: themeId })
}
function setCardPublic(id, isPublic) {
  if (!/^[0-9a-f]{32}$/.test(id) || typeof isPublic !== "boolean") throw new Error("设备信息无效")
  return request(`/devices/${id}/card`, "PATCH", { public: isPublic })
}
function unbind(id) {
  if (!/^[0-9a-f]{32}$/.test(id)) throw new Error("设备信息无效")
  return request(`/devices/${id}`, "DELETE")
}

module.exports = { pairingCode, devices, themes, claim, setTheme, setCardPublic, unbind }
