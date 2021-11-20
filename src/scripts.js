const LINE_SIZE = TIMESTAMP_SIZE + SENSOR_COUNT;
const historyBytes = hexStringToByte(_HISTORY_);
const POINTS_COUNT = historyBytes.length / LINE_SIZE;

function hexStringToByte(e) {
    if (!e) {
        return new Uint8Array()
    }
    var c = [];
    for (var d = 0, b = e.length; d < b; d += 2) {
        c.push(parseInt(e.substr(d, 2), 16))
    }
    return new Uint8Array(c)
}

function parseTime(a) {
    let time = 0;
    for (let i = 0, mult = 1; i < TIMESTAMP_SIZE; i++,
        mult *= 256) {
        time += historyBytes[a + i] * mult
    }
    return time
}
function parseTemps(a) {
    let temps = [];
    for (let i = 0; i < SENSOR_COUNT; i++) {
        temps.push(historyBytes[a + TIMESTAMP_SIZE + i])
    }
    return temps
}
function comparePoints(d, c) {
    if (d.time < c.time) {
        return -1
    }
    return d.time > c.time ? 1 : 0
}

let historyClean = [];
for (let i = 0; i < POINTS_COUNT; i++) {
    const line_start = i * LINE_SIZE;
    const time = parseTime(line_start);
    const temps = parseTemps(line_start);
    if (time == 0) {
        continue
    }
    let bad = false;
    for (let j = 0; j < temps.length; j++) {
        if (temps[j] == 157) {
            bad = true;
            break
        }
    }
    if (bad) {
        continue
    }
    let point = {
        time: time * 1000,
        temps: temps
    };
    historyClean.push(point)
}

historyClean.sort(comparePoints);
var GLOBAL_DATA = [];
var INSIDE_DATA = [];
var TOP_DATA = [];
for (let i = 0; i < historyClean.length; i++) {
    GLOBAL_DATA.push({
        x: historyClean[i].time,
        y: historyClean[i].temps[0]
    });
    INSIDE_DATA.push({
        x: historyClean[i].time,
        y: historyClean[i].temps[1]
    });
    TOP_DATA.push({
        x: historyClean[i].time,
        y: historyClean[i].temps[2]
    })
}
const annotationsOpen = _VALVEOPENINGS_.map(function (b, a) {
    return {
        type: "line",
        xMin: b,
        xMax: b,
        borderColor: "red",
        label: {
            content: "Opened",
            enabled: true,
            position: "start"
        }
    }
});
const annotationsClose = _VALVECLOSINGS_.map(function (b, a) {
    return {
        type: "line",
        xMin: b,
        xMax: b,
        borderColor: "blue",
        label: {
            content: "Closed",
            enabled: true,
            position: "start"
        }
    }
});
const annotations = annotationsOpen.concat(annotationsClose);


window.onload = function () {
    document.getElementById("_TEMP1_").innerHTML = _TEMP1_;
    document.getElementById("_TEMP2_").innerHTML = _TEMP2_;
    document.getElementById("_TEMP3_").innerHTML = _TEMP3_;

    document.getElementById("_TEMP1Q_").innerHTML = _TEMP1Q_;
    document.getElementById("_TEMP2Q_").innerHTML = _TEMP2Q_;
    document.getElementById("_TEMP3Q_").innerHTML = _TEMP3Q_;

    document.getElementById("_VALVEOPENED_").innerHTML = _VALVEOPENED_;
    document.getElementById("_VALVECLOSED_").innerHTML = _VALVECLOSED_;
    document.getElementById("_VALVESTATUS_").innerHTML = _VALVESTATUS_;

    document.getElementById("_IP_").innerHTML = _IP_;
    document.getElementById("_DST_").innerHTML = _DST_;
    document.getElementById("_TIME_").innerHTML = _TIME_;

    document.getElementById("_INFO_").innerHTML = _INFO_;

    var ctx_temps = document
        .getElementById("temperatures")
        .getContext("2d");
    var scatterChart1 = new Chart(ctx_temps, {
        type: "line",
        data: {
            datasets: [{
                label: "Global",
                data: GLOBAL_DATA,
                borderColor: "red",
                fill: false,
                tension: 0.4,
            },
            {
                label: "Inside",
                data: INSIDE_DATA,
                borderColor: "orange",
                fill: false,
                tension: 0.4,
            },
            {
                label: "Top",
                data: TOP_DATA,
                borderColor: "purple",
                fill: false,
                tension: 0.4,
            },
            ],
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: {
                x: {
                    type: "time",
                    display: true,
                    time: {
                        displayFormats: {
                            minute: "HH:mm"
                        },
                    },
                },
                y: {
                    position: "right"
                },
            },
            plugins: {
                annotation: {
                    annotations: annotations
                },
                zoom: {
                    pan: {
                        enabled: true,
                        mode: 'x',
                        speed: 20,
                        threshold: 10,
                    },

                    zoom: {
                        wheel: {
                            enabled: true,
                            modifierKey: 'ctrl',
                        },
                        drag: {
                            enabled: true,
                            backgroundColor: 'rgba(200, 200, 200, 0.6)',
                            modifierKey: 'alt',
                            borderColor: 'rgba(127,127,127)',
                            borderWidth: '1',
                        },
                        pinch: {
                            enabled: true
                        },
                        mode: 'x',

                        speed: 0.1,
                        threshold: 2,
                        sensitivity: 3,
                    },

                    limits: {
                        x: { min: 'original', max: 'original' }
                    }
                }
            }
        }
    });
};