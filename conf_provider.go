package main

import (
    "encoding/json"
    "fmt"
    "os"
    "syscall"
)

func main() {
    // Logic: Parse Config -> JSON
    // Conf files are simpler, so this binary will be smaller and faster
    data := map[string]interface{}{
        "x": 50, "y": 50, "w": 200, "h": 200,
        "type": "image", "content": "/home/matti/images/teto_idle.png",
    }
    
    payload, _ := json.Marshal(data)
    
    syscall.Exec("/home/matti/bin/ewv-engine", []string{"ewv-engine", string(payload)}, os.Environ())
}
