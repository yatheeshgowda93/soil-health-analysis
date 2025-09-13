from flask import Flask, request, jsonify, render_template, redirect, url_for, session
from datetime import datetime
import os
import secrets
import firebase_admin
from firebase_admin import credentials, db

app = Flask(__name__)
app.secret_key = secrets.token_urlsafe(32)  # Secure session key

# 🔐 Admin credentials
ADMIN_USERNAME = 'user'
ADMIN_PASSWORD = '12345678'

# 🔹 Firebase setup
KEY_PATH = r"C:\Users\MY LAP\soil_project\firebase-key.json"
if not os.path.exists(KEY_PATH):
    raise FileNotFoundError(f"❌ Firebase key file not found at: {KEY_PATH}")

cred = credentials.Certificate(KEY_PATH)
firebase_admin.initialize_app(cred, {
    'databaseURL': 'https://soil-health-project-default-rtdb.firebaseio.com/'
})

# 🔄 Store latest sensor data (init with None)
sensor_data = {
    "temperature": None,
    "soil_moisture": None,
    "ph": None,
    "nitrogen": None,
    "phosphorus": None,
    "potassium": None,
    "datetime": None
}

# 🔐 Login route
@app.route('/login', methods=['GET', 'POST'])
def login():
    if request.method == 'POST':
        username = request.form['username']
        password = request.form['password']
        if username == ADMIN_USERNAME and password == ADMIN_PASSWORD:
            session['admin'] = True
            return redirect(url_for('index'))
        return render_template('login.html', error='Invalid credentials')
    return render_template('login.html')

# 🔓 Logout route
@app.route('/logout')
def logout():
    session.pop('admin', None)
    return redirect(url_for('login'))

# 🏠 Protected index route
@app.route('/')
def index():
    if not session.get('admin'):
        return redirect(url_for('login'))
    return render_template('index.html')

# 🔄 Update sensor data (ESP32 sends here → no login required)
@app.route('/update_data', methods=['POST'])
def update_data():
    global sensor_data
    data = request.get_json()

    # Normalize keys (ESP32 may send "temp" or "pH")
    if "temp" in data:
        data["temperature"] = data.pop("temp")
    if "pH" in data:
        data["ph"] = data.pop("pH")

    # ✅ Update all fields (keep last value if missing)
    sensor_data.update({
        "temperature": data.get("temperature", sensor_data["temperature"]),
        "soil_moisture": data.get("soil_moisture", sensor_data["soil_moisture"]),
        "ph": data.get("ph", sensor_data["ph"]),
        "nitrogen": data.get("nitrogen", sensor_data["nitrogen"]),
        "phosphorus": data.get("phosphorus", sensor_data["phosphorus"]),
        "potassium": data.get("potassium", sensor_data["potassium"]),
        "datetime": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    })

    # Save to Firebase
    ref = db.reference("sensor_data")
    ref.push(sensor_data)

    # Keep only latest 10 entries
    snapshots = ref.get()
    if snapshots and len(snapshots) > 10:
        keys = list(snapshots.keys())
        for old_key in keys[:-10]:
            ref.child(old_key).delete()

    return jsonify({"status": "success", "data": sensor_data})

# 📤 Get latest sensor data
@app.route('/get_data', methods=['GET'])
def get_data():
    if not session.get('admin'):
        return redirect(url_for('login'))
    return jsonify(sensor_data)

if __name__ == '__main__':
    app.run(host="0.0.0.0", port=5000, debug=True)
