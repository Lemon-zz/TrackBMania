function draw() {
  var canvas = document.getElementById('canvas');
  if (canvas.getContext) {
    var context = canvas.getContext('2d');
        context.beginPath();
        context.rect(0, 0, 50, 50);
        context.stroke();
  }
}

