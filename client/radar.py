import tkinter as tk
import json
import os

# Configuracion del Radar
RADAR_SIZE = 400
SCALE = 5.0  # Cuanto hacer zoom (ajustar segun el mapa)

class RadarApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Project ORAS MMO - Radar")
        self.root.geometry(f"{RADAR_SIZE}x{RADAR_SIZE}")
        self.root.resizable(False, False)
        self.root.configure(bg="black")
        
        self.canvas = tk.Canvas(root, width=RADAR_SIZE, height=RADAR_SIZE, bg="#111111", highlightthickness=0)
        self.canvas.pack(fill=tk.BOTH, expand=True)
        
        # Etiqueta de ruta actual
        self.map_label = tk.Label(root, text="Buscando ruta...", fg="white", bg="#111111", font=("Arial", 12, "bold"))
        self.map_label.place(x=10, y=10)
        
        self.update_radar()
        
    def update_radar(self):
        try:
            if os.path.exists("radar_data.json"):
                with open("radar_data.json", "r") as f:
                    players = json.load(f)
                    
                self.canvas.delete("all")
                
                # Dibujar grid
                for i in range(0, RADAR_SIZE, 50):
                    self.canvas.create_line(i, 0, i, RADAR_SIZE, fill="#222222")
                    self.canvas.create_line(0, i, RADAR_SIZE, i, fill="#222222")
                    
                my_map = -1
                
                # Buscar mi mapa actual
                for pid, data in players.items():
                    if data.get("is_me"):
                        my_map = data["map"]
                        self.map_label.config(text=f"Ruta/Mapa ID: {my_map}")
                        break
                
                # Dibujar jugadores
                for pid, data in players.items():
                    # Solo mostrar si estan en la misma ruta (o si el mapa es 0 - cargando)
                    if data["map"] != my_map and my_map != -1:
                        continue
                        
                    # Mapear coordenadas (el centro es el jugador local)
                    # ORAS coord: x/y. En tkinter, y va hacia abajo.
                    # Calculamos el centro usando la pos del mapa global.
                    # (Como no tenemos los limites de todos los mapas de Hoenn, asumimos que (0,0) es el centro logico,
                    # o centramos todo relativo al mapa).
                    # Por simplicidad, dibujaremos las absolutas escaladas al centro.
                    
                    x = (data["x"] * SCALE) + (RADAR_SIZE / 2)
                    y = (data["y"] * SCALE) + (RADAR_SIZE / 2)
                    
                    color = "#00FF00" if data.get("is_me") else "#FF3366"
                    name = "TU" if data.get("is_me") else f"P{pid}"
                    
                    # Dibujar punto
                    r = 6
                    self.canvas.create_oval(x - r, y - r, x + r, y + r, fill=color, outline="white")
                    
                    # Dibujar nombre
                    self.canvas.create_text(x, y - 15, text=name, fill="white", font=("Arial", 9, "bold"))
                    
        except Exception as e:
            # Ignorar errores de lectura si el JSON se esta escribiendo en ese instante
            pass
            
        # Repetir cada 50ms (20 FPS)
        self.root.after(50, self.update_radar)

if __name__ == "__main__":
    root = tk.Tk()
    app = RadarApp(root)
    root.mainloop()
