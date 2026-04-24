# Kestirimci Motor Sağlık İzleme Sistemi

IoT tabanlı, ESP32-S3 ile motor arızalarını önceden tespit eden sistem.

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

## Görseller
![Şematik Görseli](sematik.png)
![PCB Görseli](pcb_2d.png)
![PCB Görseli](pcb_3d.png)
