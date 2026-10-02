/* Web Bluetooth is available on supported desktop/Android browsers. iPhone uses the MiniApp. */
const service = 'd6b7513e-5876-48a8-a9c5-82e89f59aa00';
const control = 'd6b7513e-5876-48a8-a9c5-82e89f59aa01';
const status = 'd6b7513e-5876-48a8-a9c5-82e89f59aa02';

for (const button of document.querySelectorAll('[data-passport-id]')) {
  if (!navigator.bluetooth) {
    button.hidden = true;
    continue;
  }
  button.addEventListener('click', async () => {
    const message = button.nextElementSibling;
    button.disabled = true;
    message.textContent = '正在连接卡片，请核对屏幕配对码…';
    let device;
    try {
      device = await navigator.bluetooth.requestDevice({ filters: [{ namePrefix: 'RDP-' }], optionalServices: [service] });
      const server = await device.gatt.connect();
      const passport = await server.getPrimaryService(service);
      const idCharacteristic = await passport.getCharacteristic(status);
      const view = await idCharacteristic.readValue();
      const bytes = new Uint8Array(view.buffer, view.byteOffset, view.byteLength);
      const id = Array.from(bytes, value => value.toString(16).padStart(2, '0')).join('');
      if (id !== button.dataset.passportId) throw new Error('连接的卡片与此账号设备不一致');
      await (await passport.getCharacteristic(control)).writeValueWithResponse(Uint8Array.of(5));
      message.textContent = '已请求卡片通过 Wi-Fi 下载所选主题，请查看卡片屏幕。';
    } catch (error) {
      message.textContent = error.message || '蓝牙连接失败';
    } finally {
      if (device && device.gatt.connected) device.gatt.disconnect();
      button.disabled = false;
    }
  });
}
