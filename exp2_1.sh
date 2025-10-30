#!/bin/sh

# Dapatkan nama file dari konfigurasi UCI
file=`uci get customized_script.general.para1`
logger "[IoT]: Starting CSV Data Logger. Output file: $file"

CHECK_INTERVAL=5
old=`date +%s`

# Fungsi untuk menulis header CSV jika file belum ada atau kosong
write_csv_header() {
    if [ ! -f "$file" ] || [ ! -s "$file" ]; then
        # Format: Waktu,ID_Node,Suhu(field1),Kelembaban(field2),RSSI
        echo "Timestamp,Node_ID,Temperature(C),Humidity(%),RSSI(dBm)" > "$file"
        logger "[IoT]: CSV Header written to $file"
    fi
}

# Panggil fungsi header sebelum loop
write_csv_header

# Run Forever
while [ 1 ]
do
	now=`date +%s`
	
	# Cek setiap 5 detik
	if [ `expr $now - $old` -gt $CHECK_INTERVAL ];then
		old=`date +%s`
		CID=`ls /var/iot/channels/ 2>/dev/null` # Dapatkan daftar channel yang ada
		
		if [ -n "$CID" ];then
			for channel in $CID; do
                
                # >>> MODIFIKASI BARU: Filter ID Channel dengan panjang kurang dari 5 <<<
                # ID Node valid (misal: 10009) memiliki 5 karakter.
                if [ ${#channel} -lt 5 ]; then
                    logger "[IoT]: INFO: Removing suspicious channel ID: $channel (Length: ${#channel})"
                    rm -f "/var/iot/channels/$channel"
                    continue # Lewati pemrosesan dan lanjut ke channel berikutnya
                fi
                # ---------------------------------------------------------------------

				# ID Node adalah nama kanal ($channel)
				NODE_ID="$channel" 
				data=`cat "/var/iot/channels/$channel" 2>/dev/null` # Baca isi channel
				
                # VALIDASI DATA KOSONG (setelah filtering ID)
                if [ -n "$data" ]; then
                    logger "[IoT]: Found $data at Local Channel:" $NODE_ID
                    
                    # Parsing data: "2025-10-30T05:40:28,-115,field1=26&field2=89"
                    
                    # 1. Ekstrak Waktu (Timestamp LoRa)
                    TIME_LORA=$(echo "$data" | awk -F ',' '{print $1}')
                    
                    # 2. Ekstrak RSSI
                    RSSI=$(echo "$data" | awk -F ',' '{print $2}')
                    
                    # 3. Ekstrak Payload "field1=26&field2=89"
                    PAYLOAD=$(echo "$data" | awk -F ',' '{print $3}')
                    
                    # 4. Ekstrak Suhu (field1)
                    TEMP=$(echo "$PAYLOAD" | awk -F '[&=]' '{print $2}')
                    
                    # 5. Ekstrak Kelembaban (field2)
                    HUM=$(echo "$PAYLOAD" | awk -F '[&=]' '{print $4}')

                    # Cek apakah semua data berhasil di-parse
                    if [ -n "$NODE_ID" ] && [ -n "$TEMP" ] && [ -n "$HUM" ] && [ -n "$RSSI" ]; then
                        # Buat baris CSV
                        CSV_LINE="$TIME_LORA"",""$NODE_ID"",""$TEMP"",""$HUM"",""$RSSI"
                        
                        logger "[IoT]: Append CSV Line: $CSV_LINE"
                        echo "$CSV_LINE" >> "$file"
                    else
                        logger "[IoT]: WARNING: Failed to parse valid components. Data was: $data"
                    fi
                else
                    # Data kosong, log diabaikan (hanya dihapus)
                    :
                fi
				
				# Hapus file channel setelah selesai diproses atau jika kosong
				rm -f "/var/iot/channels/$channel"
			done
		fi
	fi
done