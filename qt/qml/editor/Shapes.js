// The shape tool's shapes, in groups (the picker shows them as tiles; each
// tile is drawn by the shape code itself: image://ink/shape/<id>/<hex>).
.pragma library

var groups = [
    { title: "Lines", segments: true, shapes: [
        { id: "line", label: "Line" },
        { id: "arrow", label: "Arrow" },
        { id: "doubleArrow", label: "Double arrow" }] },
    { title: "Basic", shapes: [
        { id: "rectangle", label: "Rectangle" },
        { id: "roundRect", label: "Rounded rectangle" },
        { id: "ellipse", label: "Ellipse / circle" },
        { id: "triangle", label: "Triangle" },
        { id: "rightTriangle", label: "Right triangle" },
        { id: "diamond", label: "Diamond" },
        { id: "parallelogram", label: "Parallelogram" }] },
    { title: "Polygons", shapes: [
        { id: "trapezoid", label: "Trapezoid" },
        { id: "pentagon", label: "Pentagon" },
        { id: "hexagon", label: "Hexagon" },
        { id: "octagon", label: "Octagon" },
        { id: "cross", label: "Cross" }] },
    { title: "Symbols", shapes: [
        { id: "star", label: "Star" },
        { id: "heart", label: "Heart" },
        { id: "moon", label: "Moon" },
        { id: "cloud", label: "Cloud" },
        { id: "bubble", label: "Speech bubble" },
        { id: "blockArrow", label: "Block arrow" },
        { id: "lightning", label: "Lightning" }] }
]

function find(id) {
    if (id === "auto") return { id: "auto", label: "Auto shape" }
    for (var g = 0; g < groups.length; g++)
        for (var i = 0; i < groups[g].shapes.length; i++)
            if (groups[g].shapes[i].id === id) return groups[g].shapes[i]
    return null
}
function label(id) { var s = find(id); return s ? s.label : "Shape" }
function isSegment(id) { return id === "line" || id === "arrow" || id === "doubleArrow" }
