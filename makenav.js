// makenav.js 0.3.2 2007-02-27
// (C) 2006-2007 Magicant

// English version 2009-05-13

(function() {
	
	if (!("document" in this) || !document.implementation
			|| !document.implementation.hasFeature
			|| !document.implementation.hasFeature("HTML", "1.0"))
		return;
	
	var nav = document.createElement("div");
	nav.id = "magicant_auto_navigation";
	
	var heading = document.createElement("h2");
	heading.appendChild(document.createTextNode('Contents of This Page'));
	var div = document.createElement("div");
	var list = div;
	var level = 0;
	createListOfContents(document.body);
	if (div.firstChild) {
		nav.appendChild(heading);
		nav.appendChild(div);
	}
	
	function createListOfContents(e) {
		var children = e.childNodes;
		for (var i = 0; i < children.length; i++) {
			var child = children.item(i);
			if (child.nodeType == 1 /*ELEMENT_NODE*/) {
				switch (child.tagName.toLowerCase()) {
				case "h1":
					var newLevel = 1;
					break;
				case "h2":
					var newLevel = 2;
					break;
				case "h3":
					var newLevel = 3;
					break;
				case "h4":
					var newLevel = 4;
					break;
				case "h5":
					var newLevel = 5;
					break;
				case "h6":
					var newLevel = 6;
					break;
				default:
					var newLevel = 0;
					break;
				}
				if (newLevel) {
					if (level < newLevel) {
						while (++level < newLevel)
							openLevel("*");
						openLevel(getInnerText(child), child.id);
					} else {
						while (level > newLevel) {
							list = list.parentNode.parentNode;
							level--;
						}
						addHeading(list.parentNode, getInnerText(child), child.id);
					}
				} else {
					createListOfContents(child);
				}
			}
		}
		
		function openLevel(title, id) {
			var ol = document.createElement("ol");
			list.appendChild(ol);
			addHeading(ol, title, id);
		}
		function addHeading(ol, title, id) {
			var li = document.createElement("li");
			var a  = document.createElement("a");
			ol.appendChild(li);
			li.appendChild(a);
			a.appendChild(document.createTextNode(title));
			if (id) a.href = "#" + id;
			list = li;
		}
		
	}
	
	var reltypes = {
		alternate: null,
		stylesheet: null,
		start: 'Top page',
		next: 'Next',
		prev: 'Previous',
		previous: 'Previous',
		up: 'Parent',
		parent: 'Parent',
		contents: 'Table of contents',
		index: 'Index',
		glossary: 'Glossary',
		copyright: 'Copyright info',
		author: 'About author',
		appendix: 'Appendix',
		help: 'Help' //,
	};
	var heading = document.createElement("h2");
	heading.appendChild(document.createTextNode('Related resources'));
	var list = document.createElement("ul");
	var links = document.getElementsByTagName("link");
	for (var i = 0; i < links.length; i++) {
		var link = links.item(i);
		var type = reltypes[link.rel];
		if (!type || !link.href)
			continue;
		var listItem = document.createElement("li");
		list.appendChild(listItem);
		var anchor = document.createElement("a");
		listItem.appendChild(anchor);
		anchor.href = link.href;
		var anchorText = document.createTextNode(type);
		anchor.appendChild(anchorText);
		if (link.title)
			anchorText.appendData(": " + link.title);
	}
	if (list.firstChild) {
		nav.appendChild(heading);
		nav.appendChild(list);
	}
	
	if (nav.firstChild) {
		document.body.appendChild(nav);
	}
	
	function getInnerText(element) {
		var result = "", children = element.childNodes;
		for (var i = 0; i < children.length; i++) {
			var c = children.item(i);
			if (c.nodeType == 1 /*ELEMENT_NODE*/)
				if (c.tagName.toLowerCase() == "img")
					result += c.alt;
				else
					result += getInnerText(c);
			else if (c.nodeType == 3 /*TEXT_NODE*/)
				result += c.data;
		}
		return result;
	}
	
})();
