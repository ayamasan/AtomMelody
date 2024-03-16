#include <M5Atom.h>
#include <driver/i2s.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <FS.h>
#include <SPIFFS.h>
#define FILENAME "/sound.txt"

#include "sall.h"

#define CONFIG_I2S_BCK_PIN      19
#define CONFIG_I2S_LRCK_PIN     33
#define CONFIG_I2S_DATA_PIN     22
#define CONFIG_I2S_DATA_IN_PIN  23

#define SPEAKER_I2S_NUMBER      I2S_NUM_0

#define MODE_MIC                0
#define MODE_SPK                1

#define SNDLEN 16000   // 1秒分のバッファ長（×2バイトデータ）
#define WRITETIME 500  // I2S書込間隔（msec）

//  0  2  4  5  7  9 11 12 14 16 17 19 21 23 24
// ド レ ミ フ ソ ラ シ ド レ ミ フ ソ ラ シ ド

#define TEMPO 120    // 再生テンポ（120）

#define DATAMAX 250  // 演奏データ（音符）数
int melody[DATAMAX][2];
int datamax;

int sound = -1; // 0以上で演奏
int tempo = TEMPO; // テンポ（1分あたりの拍数）
double tt = 60000/tempo; // 1拍の時間（ミリ秒、テンポ120で500ミリ秒）
int rate = 16000; // サンプリングレート（160000がディフォルト）
short SONG[SNDLEN];  // 1秒分のバッファ
char csong[32000];  // 1秒分のバッファ（バイト）

int rpos = 0;  // 出力バッファ書込ポインタ
int wpos = 0;  // 出力バッファ読出ポインタ
unsigned char playbuff[2][SNDLEN];
int tskstop = 0;
int ppos = 0;  // 再生データ変換配列位置ポインタ
int playtime = 0;
int lastplaytime = 0;

// for WiFi
#define BUFSIZE 1024
int wifion = 0;  // 1=オン
// アクセスポイント設定
const char ssid[] = "xxxxxx";   // SSID
const char pass[] = "xxxxxx";  // パスワード
const int port = 60000;

// IP固定する
IPAddress ip(192, 168, 0, 212);
IPAddress gateway(192, 168, 0, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiUDP udp;

unsigned int rcvnum = 0;
int total = 0;
int count = 0;


// 受信楽曲保存
void writesong(uint8_t *data, int size) 
{
	File fp;
	
	Serial.println("SPIFFS file save.");
	// フォーマット
	SPIFFS.format();
	// ファイルを開く
	fp = SPIFFS.open(FILENAME, FILE_WRITE);
	// 書き込み
	fp.write(data, size);
	fp.close();
	Serial.println("SPIFFS witre complete.");
}

// 受信楽曲読込 戻り値：ファイルサイズ
int readsong() 
{
	int i, j;
	unsigned short d1 = 0, d2 = 0;
	unsigned short dold = 0;
	int addtime = 0;
	File fp;
	unsigned char buff[BUFSIZE];
	
	fp = SPIFFS.open(FILENAME, FILE_READ);
	if(!fp){
		Serial.printf("ERROR : SPIFFS file open error.");
		return(0);
	}
	size_t size = fp.size();
	Serial.printf("SPIFFS file size = %d\n", size);
	
	// 読み込み
	fp.readBytes((char *)buff, size);
	
	j = 0;
	for(i=0; i<6; i+=2){
		d1 = ((((unsigned short)buff[i+1]) << 8) & 0xFF00) 
		| (((unsigned short)buff[i]) & 0x00FF);
		if(i==2){
			// 音符数設定
			if(d1 > 0){
				datamax = d1 - 1;
			}
			else{
				datamax = 0;
			}
		}
		if(i==4){
			// テンポ設定
			settempo(d1);
		}
	}
	d1 = 0;
	dold = d1;
	for(i=6, j=0; i<size; i+=2, j++){
		dold = d1;
		d1 = ((((unsigned short)buff[i+1]) << 3) & 0x07F8) 
		| (((unsigned short)buff[i] >> 5) & 0x0007);
		d2 =  ((unsigned short)buff[i] & 0x001F);
		if(dold > d1){
			// 桁上がり用
			addtime += 2048;
		}
		melody[j][0] = d1 + addtime;
		melody[j][1] = d2;
	}
	
	fp.close();
	Serial.println("SPIFFS read complete.");
	return(size);
}


// UDP受信用タスク
void udpRcvTask(void* arg) 
{
	int i, j;
	int wifitimer = 0;   // 起動後30秒のみデータ受信受付用
	unsigned short d1 = 0, d2 = 0;
	unsigned short dold = 0;
	int addtime = 0;
	bool loop = true;
	unsigned char packetBuffer[BUFSIZE];
	unsigned char buff[4];
	vTaskDelay(10);
	
	while(loop){
		int packetSize = udp.parsePacket();
		
		// UDP受信有無
		if(packetSize > 0){
			// UDPデータ取得
			udp.read(packetBuffer, packetSize);
			packetBuffer[packetSize] = 0;
			
			addtime = 0;
			
			// 受信データ数
			j = 0;
			for(i=0; i<6; i+=2){
				d1 = ((((unsigned short)packetBuffer[i+1]) << 8) & 0xFF00) 
				| (((unsigned short)packetBuffer[i]) & 0x00FF);
				if(i==2){
					// 音符数設定
					if(d1 > 0){
						datamax = d1 - 1;
					}
					else{
						datamax = 0;
					}
				}
				if(i==4){
					// テンポ設定
					settempo(d1);
				}
			}
			d1 = 0;
			dold = d1;
			for(i=6, j=0; i<packetSize; i+=2, j++){
				dold = d1;
				d1 = ((((unsigned short)packetBuffer[i+1]) << 3) & 0x07F8) 
				| (((unsigned short)packetBuffer[i] >> 5) & 0x0007);
				d2 =  ((unsigned short)packetBuffer[i] & 0x001F);
				if(dold > d1){
					// 桁上がり用
					addtime += 2048;
				}
				melody[j][0] = d1 + addtime;
				melody[j][1] = d2;
			}
			
			// 送信元チェック
			IPAddress sip = udp.remoteIP();
			int sport = udp.remotePort();
			
			// 返信
			buff[0] = 'O';
			buff[1] = 'K';
			udp.beginPacket(sip, sport);
			udp.write(buff, 2);
			udp.endPacket();
			
			// ファイル保存
			writesong((uint8_t *)packetBuffer, packetSize);
			
			wifitimer = 3000;  // 30sec
		}
		vTaskDelay(10);
		
		// WiFiデータ受付30秒で終了
		wifitimer++;
		if(wifitimer > 3000){
			loop = 0;
			break;
		}
	}
	
	vTaskDelay(300);
	udp.stop();
	vTaskDelay(10);
	WiFi.disconnect();
	M5.dis.drawpix(0, CRGB(255, 255, 255));  // 白
	
	// タスク削除
	vTaskDelete(NULL);
}


void setup_wifi()
{
	int count = 0;
	
	WiFi.mode(WIFI_STA);
	WiFi.config(ip, gateway, subnet);  // static_ip
	WiFi.begin(ssid, pass);
	
	M5.dis.drawpix(0, CRGB(0, 255, 255));  // 水（WiFi接続中）
	
	wifion = 1;
	while(WiFi.status() != WL_CONNECTED){
		delay(100);
		count++;
		if((count % 2) == 0){
			M5.dis.drawpix(0, CRGB(0, 255, 255));  // 水（WiFi接続中）
		}
		else{
			M5.dis.drawpix(0, CRGB(0, 0, 0));  // 黒
		}
		if(count > 100){  // 10秒経過
			wifion = 0;  // 接続失敗
			break;
		}
	}
	
	if(wifion != 0){
		M5.dis.drawpix(0, CRGB(0, 0, 255));  // 青（WiFiオン）
		Serial.println("\nConnected");
		Serial.println(WiFi.localIP());
		delay(100);
		udp.begin(port);
	}
	else{
		M5.dis.drawpix(0, CRGB(255, 255, 255));  // 白
		delay(100);
	}
}


void InitI2SSpeakerOrMic(int mode)
{
	esp_err_t err = ESP_OK;
	
	i2s_driver_uninstall(SPEAKER_I2S_NUMBER);
	i2s_config_t i2s_config = {
		.mode                 = (i2s_mode_t)(I2S_MODE_MASTER),
		.sample_rate          = rate,
		.bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
		.channel_format       = I2S_CHANNEL_FMT_ALL_RIGHT,
		.communication_format = I2S_COMM_FORMAT_I2S,
		.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
		.dma_buf_count        = 6,
		.dma_buf_len          = 60,
		.use_apll             = false,
		.tx_desc_auto_clear   = true,
		.fixed_mclk           = 0
	};
	
	if(mode == MODE_MIC){
		i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
	}
	else{
		i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
	}
	
	err += i2s_driver_install(SPEAKER_I2S_NUMBER, &i2s_config, 0, NULL);
	
	i2s_pin_config_t tx_pin_config = {
		.bck_io_num           = CONFIG_I2S_BCK_PIN,
		.ws_io_num            = CONFIG_I2S_LRCK_PIN,
		.data_out_num         = CONFIG_I2S_DATA_PIN,
		.data_in_num          = CONFIG_I2S_DATA_IN_PIN,
	};
	
	err += i2s_set_pin(SPEAKER_I2S_NUMBER, &tx_pin_config);
	
	if(mode != MODE_MIC){
		err += i2s_set_clk(SPEAKER_I2S_NUMBER, rate, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
	}
	
	i2s_zero_dma_buffer(SPEAKER_I2S_NUMBER);
}


// 再生用タスク
void i2sPlayTask(void* arg) {
	size_t bytes_written;
	bool loop = true;
	vTaskDelay(1);
	
	while(loop){
		if(sound >= 0){
			// Write Speaker
			i2s_write(SPEAKER_I2S_NUMBER, playbuff[rpos], SNDLEN, &bytes_written, portMAX_DELAY);
			
			if(rpos == 0) rpos = 1;
			else          rpos = 0;
		}
		
		if(tskstop != 0){
			// 終了
			loop = false;
			break;
		}
		vTaskDelay(1);
	}
	
	rpos = 0;
	tskstop = 0;
	Serial.println("STOP i2sPlayTask.");
	vTaskDelay(10);
	M5.dis.drawpix(0, CRGB(255, 255, 255));  // 白
	
	// タスク削除
	vTaskDelete(NULL);
}


void settempo(int t)
{
	tempo = t;
	tt = 60000.0 / (double)tempo;
}


// return : 発音音符数　0以上=継続演奏、-1=演奏終了（データエンド）
int makesound()
{
	int i;
	int ss;
	double t64;
	double tx;
	int sspos;
	int dd;
	int pp;
	double gain;
	
	// 64分音符1つ分の時間
	t64 = 60000.0 / (double)(tempo * 16);  // msec
	
	// 何音出るか調べる
	pp = ppos;
	ss = 0;
	while(1){
		// 音符を演奏する時間（msec）
		tx = ((double)melody[pp][0] * t64);
		if(tx < (double)playtime){
			ss++;  // 音符数加算
			pp++;
			if(pp >= datamax){
				break;
			}
		}
		else{
			break;
		}
	}
	gain = 0.3;
	
	// 音作成
	while(1){
		// データ作成
		// 音符を演奏する時間（msec）
		tx = ((double)melody[ppos][0] * t64);
		// 今演奏をすべき時間内にある音か判断
		if(tx < (double)playtime){
			// 1転送分（500msec）内の発音
			// SONG[]バッファ上の位置
			sspos = (int)(16.0 * (tx - (double)lastplaytime));  // 16000 / 500 * tx;
			if(sspos > SNDLEN/2){
				Serial.println("ERROR : sspos > SNDLEN");
			}
			else{
				// SONG[]に新たな音を加算する
				for(i=0; i<SNDLEN/2; i++){
					dd = (int)SONG[sspos+i] + (int)s[melody[ppos][1]][i];
					// 同時発音数でゲイン調整
					if(gain < 1.0){
						dd = (int)((double)dd * gain);
					}
					// オーバーフローリミッタ
					if(dd > 32767){
						SONG[sspos+i] = 32767;
					}
					else if(dd < -32768){
						SONG[sspos+i] = -32768;
					}
					else{
						SONG[sspos+i] = dd;
					}
				}
			}
			ppos++;
			if(ppos >= datamax){
				return(-1);  // 演奏終了
			}
		}
		else{
			// 次の時間（発音分）
			return(ss);  // 継続演奏
		}
	}
	
	return(0);
}


void setup() 
{
	M5.begin(true, false, true);
	int i;
	
	delay(500);
	for(i=0; i<3; i++){  // 白、赤点滅3回
		M5.dis.drawpix(0, CRGB(255, 255, 255));
		delay(300);
		M5.dis.drawpix(0, CRGB(255, 0, 0));
		delay(300);
	}
	
	for(i=0; i<DATAMAX; i++){
		melody[i][0] = 0;
		melody[i][1] = 0;
	}
	datamax = 0;
	
	memset((char *)&SONG[0], 0, SNDLEN*2);
	sound = -1;
	tempo = TEMPO;
	settempo(tempo);
	
	InitI2SSpeakerOrMic(MODE_MIC);
	
	SPIFFS.begin();
	delay(50);
	
	// 演奏曲データありなら白点灯、無ければ消灯
	if(readsong() == 0){
		M5.dis.drawpix(0, CRGB(0, 0, 0));  // 黒
	}
	else{
		M5.dis.drawpix(0, CRGB(255, 255, 255));  // 白
	}
	
	// for WiFi
	setup_wifi();
	delay(1000);
	
	if(wifion != 0){
		// 起動後30秒で自動終了
		xTaskCreatePinnedToCore(udpRcvTask, "udpRcvTask", 4096, NULL, 1, NULL, 1);
	}
}


void loop() 
{
	int i, j;
	int ss;
	M5.update();
	
	// ボタン押し
	if(M5.Btn.wasPressed()){
		M5.dis.drawpix(0, CRGB(255, 0, 0));  // 赤
		InitI2SSpeakerOrMic(MODE_SPK);
		
		Serial.printf("BUTTON PUSH. tt=%f\n", tt);
		
		// SONG[]書込
		wpos = 0;
		// 発音範囲を加算
		ppos = 0;
		
		Serial.printf("NUM = %d\n", datamax);
		Serial.printf("TEMPO = %d\n", tempo);
		//for(i=0; i<datamax; i++){
		//	Serial.printf("[%d] %d, %d\n", i, melody[i][0], melody[i][1]);
		//}
		
		j = 0;
		for(i=0; i<datamax; i++){
			if(melody[i][0] == 0){
				if(j != 0){
					// またゼロ検出時は、そこで演奏終了
					datamax = i;
					Serial.printf("MOD NUM = %d\n", datamax);
					break;
				}
			}
			else{
				// 0でないタイミングを検出
				j = 1;
			}
		}
		
		lastplaytime = 0;
		playtime = WRITETIME;  // I2S書込サイズ（500msec）
		ss = makesound();  // SONG[]に音声500msec分の発音を合成
		lastplaytime = playtime;
		playtime += WRITETIME;
		// 転送バッファにコピー
		memcpy(playbuff[wpos], (char *)&SONG[0], SNDLEN);
		wpos = 1;
		// SONG[]バッファを次のデータに備える
		memcpy((char *)&SONG[0], (char *)&SONG[SNDLEN/2], SNDLEN);
		memset((char *)&SONG[SNDLEN/2], 0, SNDLEN);
		
		Serial.println("START i2sPlayTask.");
		// 再生タスク起動
		xTaskCreatePinnedToCore(i2sPlayTask, "i2sPlayTask", 4096, NULL, 1, NULL, 1);
		delay(10);
		
		if(ss >= 0){
			sound = ss;
		}
	}
	
	// 演奏
	// 読出バッファが切り替わったら（再生済みのバッファに）書込をおこなう
	if(sound >= 0 && wpos != rpos){
		Serial.printf("PLAY[%d]\n", sound);
		
		// Write Speaker（データ転送完了まで待たされる）
		if(sound > datamax){
			// 演奏（タスク）終了
			tskstop = 1;
			sound = -1;  // 終了
			wpos = 0;
			delay(WRITETIME + 100);  // 最後の音が再生が終わるまで待つ(>500msec)
			Serial.println("PLAY END.");
			// Set Mic Mode
			InitI2SSpeakerOrMic(MODE_MIC);
			M5.dis.drawpix(0, CRGB(255, 255, 255));  // 白
		}
		else if(sound == datamax){
			// 無音作成（最後に無音を再生するため）
			for(i=0; i<SNDLEN; i++){
				playbuff[wpos][i] = 0;
			}
			if(wpos == 0) wpos = 1;
			else          wpos = 0;
			sound++;
		}
		else{
			// 次の500msec分の音を作成
			ss = makesound();  // SONG[]に音声500msec分の発音を合成
			if(ss < 0){
				// 演奏終了
				sound = datamax;
			}
			else{
				sound += ss;
			}
			lastplaytime = playtime;
			playtime += WRITETIME;
			// 転送バッファにコピー
			memcpy(playbuff[wpos], (char *)&SONG[0], SNDLEN);
			// SONG[]バッファを次のデータに備える
			memcpy((char *)&SONG[0], (char *)&SONG[SNDLEN/2], SNDLEN);
			memset((char *)&SONG[SNDLEN/2], 0, SNDLEN);
			if(wpos == 0) wpos = 1;
			else          wpos = 0;
		}
	}
	delay(1);
}

