# FreshAgro project flow

```mermaid
flowchart TD
    subgraph Device[ESP32-C3 sensor node — 10-minute start-to-start cycle]
        A[Timer wake] --> B[Initialize UART and I2C] --> C[Wait 2 seconds for continuously powered sensors]
        C --> D[Read O₂ over UART and validate health/status/range]
        D --> E[Read CO₂ over UART and validate reading/scale]
        E --> F[Read SHT3x over I2C and validate CRC]
        F --> G{How many sensor devices valid?}
        G -->|3| H[Green sensor check — 1 second]
        G -->|1 or 2| I[Orange sensor check — 1 second]
        G -->|0| J[Red sensor check — 1 second]
        H --> K[Save cycle to persistent NVS queue]
        I --> K
        J --> K
        K --> L{Network configured and reachable?}
        L -->|No| M[Internet check red — 1 second]
        L -->|Yes| N[Connect Wi-Fi; synchronize clock if needed]
        N --> O[Send authenticated HTTPS batch to Supabase]
        O --> P{Certificate-verified HTTPS response?}
        P -->|No| Q[Internet check red — 1 second]
        P -->|Yes| R[Internet check green — 1 second]
        Q --> S{HTTP 200 durable batch acknowledgement?}
        R --> S
        S -->|No| T[Data-sent check red — 0.5 second; keep queue]
        S -->|Yes| U[Data-sent check green — 0.5 second; acknowledge queue]
        M --> V[Turn off Wi-Fi and LED]
        T --> V
        U --> V
        V --> W[Deep sleep for remaining interval]
        W --> A
    end

    subgraph Cloud[Hosted services — independent of laptop/device sleep]
        O --> X[Supabase Edge Function checks device token]
        X --> Y[Validate batch and insert idempotently into Postgres]
        Y --> Z[Return HTTP 200 only after durable insert]
        Z --> S
        Y --> AA[Public read-only telemetry query]
        AA --> AB[Vercel dashboard: latest values, history, health, CSV export]
    end
```

The queue retains readings when an upload fails and retries them later. Server-side sample IDs prevent duplicate rows after a lost acknowledgement. The 72-cycle queue, timing and LED outcomes are implemented in the cloud sketch. Sensor supplies remain on; deep sleep alone does not power down the external sensors or board regulator.
