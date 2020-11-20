# wb-mqtt-iec104
MQTT to IEC 60870-5-104 gateway which follows [Wiren Board MQTT Conventions](https://github.com/contactless/homeui/blob/master/conventions.md).
It's designed to be used on [Wiren Board](http://contactless.ru/en/) family of programmable automation controllers (PACs).

Шлюз предназначен для трансляции сообщений между MQTT брокером и системами с поддержкой протокола [МЭК 60870-5-104](https://ru.wikipedia.org/wiki/IEC_60870-5).
Шлюз предназначен для устройств [Wiren Board](http://contactless.ru/en/) и соответствует [Конвенции Wiren Board MQTT](https://github.com/contactless/homeui/blob/master/conventions.md).

Запускается командой `systemctl start wb-mqtt-iec104` или `service wb-mqtt-iec104 start`

По умолчанию запуск шлюза происходит при загрузке системы. При запуске шлюза происходит автоматическое создание конфигурационного файла `/etc/wb-mqtt-iec104.conf`. При последующих запусках шлюз анализирует доступные MQTT каналы(контролы) и добавляет их в файл. Активировать передачу данных конкретных каналов можно, редактируя `/etc/wb-mqtt-serial.conf`, либо воспользовавшись онлайн-редактором настроек.

Шлюз подключается к заданому MQTT брокеру и подписывается на сообщения от каналов, указанных в конфигурационном файле. В системах с поддержкой протокола МЭК 60870-5-104 шлюз выступает в роли контролируемой станции и принимает входящие TCP/IP соединения по указаному в конфигурационном файле локальному интерфейсу и порту.

Возможен запуск шлюза вручную, что может быть полезно для работы в отладочном режиме:
```
# service wb-mqtt-iec104 stop
# wb-mqtt-iec104 -d 3
```

Структура конфигурационного файла
---------------------------------
```
{
  // Включает или выключает выдачу отладочной информации во время работы шлюза.
  // Опция -d командной строки шлюза также включает отладочную печать и имеет больший приоритет.
  "debug" : false,

  // Настройки протокола МЭК 60870-5-104. Обязательный параметр.
  "iec104" :
  {
    // Общий адрес станции для шлюза. Обязательный параметр.
    "address" : 1,

    // Локальный IP адрес, на котором шлюз будет ожидать входящие соединения. Обязательный параметр.
    // Если задана пустая строка, то шлюз будет ожидать соединения по всем доступным локальным адресам.
    "host" : "",

    // Порт для входящих соединений. Обязательный параметр.
    "port" : 2404
  },

  // Настройки подключения к MQTT брокеру.
  // Если не указаны, то используются настройки по умолчанию.
  "mqtt" :
  {
    // Доменное имя или IP адрес брокера. По умолчанию, "localhost".
    "host" : "localhost",

    // Порт брокера. По умолчанию, 1883.
    "port" : 1883,

    // Интервал посылки keep-alive сообщений в секундах. По умолчанию, 60.
    "keepalive": 60,

    // Требуется ли аутентификация при подключении к брокеру. По умолчанию, false.
    "auth": false,

    // Имя пользователя при подключении к брокеру. По умолчанию, пустая строка.
    "username": ""

    // Пароль при подключении к брокеру. По умолчанию, пустая строка.
    "password": ""
  }

  // Список групп каналов, для которых осуществляется пересылка сообщений.
  // Используется для логической группировки в интерфейсе онлайн-редактора настроек.
  // Позволяет включать/отключать пересылку сообщений для нескольких каналов, редактируя только один параметр "enabled".
  "groups" :
  [
    {
      // Включение/отключение пересылки сообщений каналов группы.
      // Для активации пересылки надо включить конкретные каналы.
      "enabled" : true,
  
      // Имя группы.
      "name" : "buzzer",

      // Список каналов в группе.
      "controls" :
      [
        {
          // Включение/отключение пересылки сообщений канала.
          // Также должа быть включена пересылка на уровне группы.
          "enabled" : true,

          // Имя устройства и канала в терминах MQTT конвенции, разделённые символом "/".
          // В конкретном случае, соответствующая MQTT-тема /devices/buzzer/controls/volume
          "topic" : "buzzer/volume",

          // Адрес объекта информации согласно МЭК 60870-5-104 для конкретного канала.
          // Адрес может выбираться из интервала [1, 16777215] и должен быть уникальным.
          // При генерации и обновлении конфигурационного файла шлюз автоматически выбирает адреса для каждого канала.
          // В дальнейшем адрес можно изменить, редактируя конфигурационный файл или через онлайн-редактор настроек.
          "address" : 1,

          // Идентификатор типа блока информации согласно МЭК 60870-5-104
          // Один из вариантов:
          //  "single" - одноэлементная информация (М_SP_NA_1)
          //  "short"  - значение измеряемой величины, короткий формат с плавающей запятой (М_ME_NC_1)
          //  "scaled" - масштабированное значение измеряемой величины (М_ME_NB_1)
          "iec_type" : "short",

          // Тип канала (/devices/+/controls/+/meta/type) и возможность записи в него (/devices/+/controls/+/meta/readonly).
          // Используется для информации в интерфейсе онлайн-редактора настроек. Не имеет влияния на работу шлюза.
          "info" : "range (setup is allowed)"
        },
        ...
      ]
    },
    ...
  ]
}
```

Передача сообщений из MQTT в МЭК 60870-5-104
--------------------------------------------
Сообщения MQTT передаются в МЭК 60870-5-104 блоками данных (ASDU) с причиной передачи "спорадически"(3). При подключении нового контролирующего устройства, шлюз автоматически высылает последние известные значения всех включенных каналов. В дальнейшем каждое новое MQTT-сообщение сразу же передаётся в МЭК 60870-5-104.

Передача команд МЭК 60870-5-104 в MQTT
--------------------------------------------
Шлюз поддерживает ASDU с типами:
- одноэлементная команда (C_SC_NA_1);
- команда уставки, масштабированное значение (C_SE_NB_1);
- команда уставки, короткое число с плавающей запятой (C_SE_NC_1).

Обрабатывается первый объект информации в ASDU. Если в конфигурационном файле есть включенный канал для адреса этого объекта информации, шлюз произведёт запись полученного значения в соответствующую тему канала (например, /devices/wb-gpio/controls/5V_OUT/on).
Команды опроса и прочие команды не поддерживаются.