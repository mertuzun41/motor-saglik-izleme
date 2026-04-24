# Kestirimci Motor Sağlık İzleme Sistemi

IoT tabanlı, ESP32-S3 ile motor arızalarını önceden tespit eden sistem.

---

## Donanım
- ESP32-S3 mikrodenetleyici
- MPU6050 — titreşim sensörü
- INA219 — akım/voltaj ölçümü  
- DS18B20 — sıcaklık sensörü
- TB6612FNG — motor sürücü

## Özellikler
- Gerçek zamanlı sensör verisi işleme
- Cihaza gömülü web arayüzü (WebSocket)
- Özgün PCB tasarımı

## Kurulum
1. Arduino IDE'ye ESP32 kütüphanesini ekle
2. Web arayüzüne tarayıcıdan `192.168.4.1` ile bağlan

---

## Görseller

### Şematik
![Şematik](sematik.png)

---

### PCB Tasarımı

<table>
  <tr>
    <td align="center"><b>2D Görünüm</b></td>
    <td align="center"><b>3D Görünüm</b></td>
  </tr>
  <tr>
    <td><img src="pcb_2d.png" width="400"/></td>
    <td><img src="pcb_3d.png" width="400"/></td>
  </tr>
</table>

---

### Üretim

<table>
  <tr>
    <td align="center"><b>Kart - Ön</b></td>
    <td align="center"><b>Kart - Arka</b></td>
  </tr>
  <tr>
    <td><img src="motorsaglik_kart1.jpeg" width="400"/></td>
    <td><img src="motorsaglik_kart2.jpeg" width="400"/></td>
  </tr>
</table>

---

### Web Arayüzü
![Web Arayüzü](motor_web.png)
