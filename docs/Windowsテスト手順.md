# Windowsテスト手順（AP固定IP: 192.168.4.1）

前提:
- PCをESP32のAP（SSID: esp32-oven）に接続
- PowerShellを使用

## 1) ステータス
```powershell
Invoke-RestMethod http://192.168.4.1/api/status
```

## 2) プロファイル作成
```powershell
$body = @{
  name = "profile1"
  end_behavior = "hold_last"
  points = @(
    @{ t_sec = 0; temp_c = 25 }
    @{ t_sec = 60; temp_c = 120 }
    @{ t_sec = 180; temp_c = 150 }
  )
} | ConvertTo-Json -Depth 5

Invoke-RestMethod http://192.168.4.1/api/profiles -Method Post -ContentType "application/json" -Body $body
```

## 3) 一覧/取得
```powershell
Invoke-RestMethod http://192.168.4.1/api/profiles
Invoke-RestMethod http://192.168.4.1/api/profiles/profile1
```

## 4) Run/Stop
```powershell
$run = @{ profile_id = "profile1" } | ConvertTo-Json
Invoke-RestMethod http://192.168.4.1/api/run -Method Post -ContentType "application/json" -Body $run
Invoke-RestMethod http://192.168.4.1/api/stop -Method Post
```

## 5) 削除
```powershell
Invoke-RestMethod http://192.168.4.1/api/profiles/profile1 -Method Delete
```
